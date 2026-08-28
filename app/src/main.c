#include "main.h"

#include "app_config.h"
#include "drv_board.h"
#include "driver.h"
#include "debug.h"

#include <string.h>

/* ISR事件位：1ms节拍。 */
#define APP_EVENT_TICK                              (1UL << 0)
/* ISR事件位：至少一个ADC DMA半块完成。 */
#define APP_EVENT_ADC                               (1UL << 1)
/* ISR事件位：快速故障等待主循环锁存。 */
#define APP_EVENT_FAST_FAULT                        (1UL << 2)
/* ISR事件位：UART RX环形缓冲中有待处理数据。 */
#define APP_EVENT_UART_RX                           (1UL << 3)

/* 看门狗健康票据：主循环正常推进。 */
#define APP_WDG_TICKET_MAIN                         (1UL << 0)
/* 看门狗健康票据：功率运行状态下ADC仍持续发布。 */
#define APP_WDG_TICKET_ADC                          (1UL << 1)
/* 看门狗健康票据：1ms应用控制链正常执行。 */
#define APP_WDG_TICKET_CONTROL                      (1UL << 2)

/* PWM授权状态：完全关闭。 */
#define APP_PWM_ARM_OFF                             (0U)
/* PWM授权状态：等待零CCR在自然UPDATE边界装载。 */
#define APP_PWM_ARM_WAIT_ZERO                       (1U)
/* PWM授权状态：硬件已通过复核并放行。 */
#define APP_PWM_ARM_ACTIVE                          (2U)

/* 应用运行时全局上下文，供目标中断向量发布事件。 */
aurora_app_runtime_t g_aurora_app_runtime;

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t estimate_efficiency_q15(const aurora_measurement_t *sample)
 * Input       : sample - 最新测量快照
 * Output      : 当前功率区间的保守效率估计，Q15
 * Description : 无BAT_I通道时按PV功率分段估算效率；实测效率图确认后只调整集中参数，不改控制结构。
 *---------------------------------------------------------------------------*/
static uint16_t estimate_efficiency_q15(const aurora_measurement_t *sample)
{
    if (sample->pv_power_mw < AURORA_EFFICIENCY_LOW_LIMIT_MW)
    {
        return AURORA_EFFICIENCY_LOW_Q15;
    }
    if (sample->pv_power_mw < AURORA_EFFICIENCY_MID_LIMIT_MW)
    {
        return AURORA_EFFICIENCY_MID_Q15;
    }
    return AURORA_EFFICIENCY_HIGH_Q15;
}

/*---------------------------------------------------------------------------*
 * Name        : static void atomic_or_u32(volatile uint32_t *target, uint32_t value)
 * Input       : target - 目标值；value - 输入数值
 * Output      : 无
 * Description : 在短临界区内原子地把事件位合并到共享位图。
 *---------------------------------------------------------------------------*/
static void atomic_or_u32(volatile uint32_t *target, uint32_t value)
{
    aurora_irq_state_t irq = drv_irq_save();
    *target |= value;
    drv_irq_restore(irq);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t atomic_exchange_u32(volatile uint32_t *target, uint32_t value)
 * Input       : target - 目标值；value - 输入数值
 * Output      : 被替换前的原始32位值
 * Description : 在短临界区内原子读取并替换共享32位值。
 *---------------------------------------------------------------------------*/
static uint32_t atomic_exchange_u32(volatile uint32_t *target, uint32_t value)
{
    uint32_t previous;
    aurora_irq_state_t irq = drv_irq_save();
    previous = *target;
    *target = value;
    drv_irq_restore(irq);
    return previous;
}

/*---------------------------------------------------------------------------*
 * Name        : static void force_safe_off(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : 统一关闭PWM、暂存零占空比并复位应用运行时发波授权状态。
 *---------------------------------------------------------------------------*/
static void force_safe_off(aurora_app_runtime_t *runtime)
{
    drv_pwm_disarm();
    (void)drv_pwm_stage_duty(0U, NULL);
    runtime->pwm_arm_state = APP_PWM_ARM_OFF;
}

/*---------------------------------------------------------------------------*
 * Name        : static bool safety_still_clear(const aurora_app_runtime_t *runtime, uint32_t token)
 * Input       : runtime - 应用运行时上下文；token - 安全epoch快照
 * Output      : true表示epoch、软件保护、Break源/锁存和人工总门均保持安全
 * Description : 复核安全epoch、待处理故障、软件保护、硬件Break和人工功率门禁，防止旧授权跨故障继续使用。
 *---------------------------------------------------------------------------*/
static bool safety_still_clear(const aurora_app_runtime_t *runtime, uint32_t token)
{
    return (runtime->safety_epoch == token) &&
           (runtime->pending_fault_mask == 0U) &&
           aurora_protection_is_safe(&runtime->app.protection) &&
           !drv_pwm_break_source_active() &&
           !drv_pwm_break_latched() &&
           drv_board_power_gate_open();
}

/*---------------------------------------------------------------------------*
 * Name        : static void apply_power_command(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : 把应用层功率命令落实到继电器和PWM；首次发波先等待零CCR自然UEV，运行中只写shadow，任一复核失败立即关波。
 *---------------------------------------------------------------------------*/
static void apply_power_command(aurora_app_runtime_t *runtime)
{
    const aurora_power_command_t *command = &runtime->app.power_command;
    uint32_t token;
    aurora_irq_state_t irq;

    drv_io_set_relay(command->relay_enable);

    if (!command->pwm_enable || !aurora_protection_is_safe(&runtime->app.protection))
    {
        force_safe_off(runtime);
        return;
    }

    if (runtime->pwm_arm_state == APP_PWM_ARM_OFF)
    {
        /* 首次放行前先写0到CCR预装载，并等待至少一个自然UEV。 */
        drv_pwm_disarm();
        if (drv_pwm_prepare_arm_zero(&runtime->pwm_zero_sequence))
        {
            runtime->pwm_arm_state = APP_PWM_ARM_WAIT_ZERO;
        }
        return;
    }

    if (runtime->pwm_arm_state == APP_PWM_ARM_WAIT_ZERO)
    {
        if (drv_pwm_applied_sequence() < runtime->pwm_zero_sequence)
        {
            return;
        }

        token = runtime->safety_epoch;
        irq = drv_irq_save();
        if (!safety_still_clear(runtime, token) || !drv_pwm_arm())
        {
            drv_irq_restore(irq);
            force_safe_off(runtime);
            return;
        }
        drv_irq_restore(irq);

        /* 退出临界区后再查一次；期间即使CPU中断被屏蔽，硬件Break仍可直接关波。 */
        if (!safety_still_clear(runtime, token) || !drv_pwm_output_active())
        {
            force_safe_off(runtime);
            return;
        }
        runtime->pwm_arm_state = APP_PWM_ARM_ACTIVE;
        return;
    }

    token = runtime->safety_epoch;
    if (!safety_still_clear(runtime, token))
    {
        force_safe_off(runtime);
        return;
    }

    /* 仅写CCR shadow；drv_pwm_stage_duty禁止运行期软件UG。 */
    if (!drv_pwm_stage_duty(command->duty_q15, NULL) || !safety_still_clear(runtime, token))
    {
        force_safe_off(runtime);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_adc(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : 原子领取DMA完成块并标记处理中，逐块完成物理量处理；同半块被DMA追上时由ISR锁存overrun。
 *---------------------------------------------------------------------------*/
static void process_adc(aurora_app_runtime_t *runtime)
{
    uint8_t mask;
    uint8_t index;
    aurora_irq_state_t irq;

    /*
     * 原子取得已完成块，同时标记为“主循环处理中”。DMA ISR若在处理期间
     * 再次完成同一半缓冲，必须报告overrun，不能静默覆盖正在读取的数据。
     */
    irq = drv_irq_save();
    mask = runtime->adc_completed_mask;
    runtime->adc_completed_mask = 0U;
    runtime->adc_processing_mask |= mask;
    drv_irq_restore(irq);

    for (index = 0U; index < 2U; ++index)
    {
        const uint8_t bit = (uint8_t)(1U << index);
        if ((mask & bit) != 0U)
        {
            const uint16_t *block = drv_adc_completed_block(index);
            if (block != NULL)
            {
                aurora_app_on_adc_block(&runtime->app,
                                        block,
                                        drv_adc_block_words(),
                                        runtime->adc_timestamp_ms[index]);
                if (runtime->app.sample.sequence == 1U)
                {
                    DEBUG_NTC_PRINTF("mos_valid=%u mos_temp_dc=%d",
                                     (unsigned)((runtime->app.sample.valid_mask &
                                                 AURORA_MEAS_VALID_MOS_TEMP) != 0U),
                                     (int)runtime->app.sample.mos_temp_dC);
                }
                runtime->watchdog_seen |= APP_WDG_TICKET_ADC;
            }

            irq = drv_irq_save();
            runtime->adc_processing_mask &= (uint8_t)~bit;
            drv_irq_restore(irq);
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_uart(aurora_app_runtime_t *runtime, uint32_t now_ms)
 * Input       : runtime - 应用运行时上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 按主循环预算消费RX环形缓冲、推进协议解析并发送应答；余量未处理完时重新投递事件。
 *---------------------------------------------------------------------------*/
static void process_uart(aurora_app_runtime_t *runtime, uint32_t now_ms)
{
#if (BOARD_USART_MODE == BOARD_USART_MODE_DEBUG)
    (void)runtime;
    (void)now_ms;
    /* Debug路由仅输出日志，禁止把调试输入误送入产品协议解析器。 */
    return;
#else
    aurora_protocol_frame_t request;
    aurora_protocol_frame_t response;
    bool has_response;
    uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
    size_t wire_length;
    uint32_t budget = BOARD_UART_APP_RX_BUDGET;

    while (budget > 0U)
    {
        uint8_t byte;
        aurora_irq_state_t irq = drv_irq_save();
        if (runtime->uart_tail == runtime->uart_head)
        {
            drv_irq_restore(irq);
            break;
        }
        byte = runtime->uart_rx[runtime->uart_tail];
        runtime->uart_tail = (uint16_t)((runtime->uart_tail + 1U) % sizeof(runtime->uart_rx));
        drv_irq_restore(irq);
        budget--;

        aurora_protocol_feed_byte(&runtime->app.protocol, byte, now_ms);

        /* 每完成一帧立即取走，避免同一批RX数据中的后一帧覆盖前一帧。 */
        if (aurora_protocol_take_frame(&runtime->app.protocol, &request))
        {
            aurora_app_on_protocol_frame(&runtime->app,
                                         &request,
                                         &response,
                                         &has_response,
                                         now_ms);
            if (has_response)
            {
                wire_length = aurora_protocol_encode(&response, wire, sizeof(wire));
                if (wire_length != 0U)
                {
                    (void)drv_uart_send(wire, wire_length);
                }
            }
            if ((request.resource == AURORA_PROTOCOL_RESOURCE_SETTING) &&
                (request.action == AURORA_PROTOCOL_ACTION_WRITE))
            {
                DEBUG_BLE_PRINTF("setting result=%u chemistry=%u pack=%u",
                                 (unsigned)((has_response) ? response.data[0] :
                                                       AURORA_PROTOCOL_RESULT_INVALID),
                                 (unsigned)runtime->app.storage.settings.chemistry,
                                 (unsigned)runtime->app.storage.settings.pack);
            }
        }
    }

    /* 尚有字节时重新投递事件，避免一次主循环被通信长帧长期占用。 */
    if (runtime->uart_tail != runtime->uart_head)
    {
        atomic_or_u32(&runtime->event_flags, APP_EVENT_UART_RX);
    }
#endif
}

/*---------------------------------------------------------------------------*
 * Name        : static void load_storage(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : 读取并校验Flash A/B页，按回绕安全序号选择较新记录并应用到应用层。
 *---------------------------------------------------------------------------*/
static void load_storage(aurora_app_runtime_t *runtime)
{
    uint8_t page_a[AURORA_STORAGE_PAGE_SIZE];
    uint8_t page_b[AURORA_STORAGE_PAGE_SIZE];
    aurora_persistent_settings_t settings_a = {0};
    aurora_persistent_settings_t settings_b = {0};
    uint32_t seq_a = 0U;
    uint32_t seq_b = 0U;
    bool valid_a;
    bool valid_b;

    valid_a = drv_flash_read(drv_board_flash_page_a(), page_a, sizeof(page_a)) &&
              aurora_storage_decode_page(page_a, sizeof(page_a), &settings_a, &seq_a);
    valid_b = drv_flash_read(drv_board_flash_page_b(), page_b, sizeof(page_b)) &&
              aurora_storage_decode_page(page_b, sizeof(page_b), &settings_b, &seq_b);

    if (valid_a || valid_b)
    {
        if (valid_b && (!valid_a || ((int32_t)(seq_b - seq_a) > 0)))
        {
            runtime->app.storage.settings = settings_b;
            runtime->app.storage.sequence = seq_b;
        }
        else
        {
            runtime->app.storage.settings = settings_a;
            runtime->app.storage.sequence = seq_a;
        }
        aurora_app_apply_settings(&runtime->app, &runtime->app.storage.settings, drv_time_now_ms());
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void app_storage(aurora_app_runtime_t *runtime, uint32_t now_ms)
 * Input       : runtime - 应用运行时上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 在脏数据稳定、PWM关闭且继电器断开时执行双页Journal保存，并把Commit Marker作为最后一步写入。
 *---------------------------------------------------------------------------*/
static void app_storage(aurora_app_runtime_t *runtime, uint32_t now_ms)
{
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t target;
    size_t used;

    if (!runtime->app.storage.dirty ||
        ((now_ms - runtime->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        drv_pwm_output_active() || runtime->app.power_stage.relay_closed)
    {
        return;
    }

    runtime->app.storage.sequence++;
    target = ((runtime->app.storage.sequence & 1U) != 0U) ?
                 drv_board_flash_page_a() : drv_board_flash_page_b();
    used = aurora_storage_encode_page(&runtime->app.storage, page, sizeof(page), false);
    if ((used < AURORA_STORAGE_HEADER_SIZE) || !drv_flash_erase_page(target) ||
        !drv_flash_program(target, page, AURORA_STORAGE_COMMIT_OFFSET) ||
        !drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t),
                           &page[AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t)],
                           used - AURORA_STORAGE_COMMIT_OFFSET - sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&runtime->app.protection,
                                           AURORA_FAULT_STORAGE,
                                           now_ms);
        DEBUG_STORAGE_PRINTF("write_failed address=0x%08lx",
                             (unsigned long)target);
        return;
    }

    /* Commit marker最后写入；中途掉电时旧页仍然有效。 */
    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET,
                           &((uint32_t){AURORA_STORAGE_COMMIT_MARKER}),
                           sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&runtime->app.protection,
                                           AURORA_FAULT_STORAGE,
                                           now_ms);
        DEBUG_STORAGE_PRINTF("commit_failed address=0x%08lx",
                             (unsigned long)target);
        return;
    }
    runtime->app.storage.dirty = false;
    DEBUG_STORAGE_PRINTF("saved sequence=%lu address=0x%08lx",
                         (unsigned long)runtime->app.storage.sequence,
                         (unsigned long)target);
}

/*---------------------------------------------------------------------------*
 * Name        : static void app_watchdog(aurora_app_runtime_t *runtime, uint32_t now_ms)
 * Input       : runtime - 应用运行时上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 按时间窗核对主循环、控制和功率状态下的ADC心跳；只有全部票据齐全时才喂IWDT。
 *---------------------------------------------------------------------------*/
static void app_watchdog(aurora_app_runtime_t *runtime, uint32_t now_ms)
{
    uint32_t required = APP_WDG_TICKET_MAIN | APP_WDG_TICKET_CONTROL;

    if ((runtime->app.power_stage.state == AURORA_POWER_PRECHARGE) ||
        (runtime->app.power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (runtime->app.power_stage.state == AURORA_POWER_RUN))
    {
        required |= APP_WDG_TICKET_ADC | APP_WDG_TICKET_CONTROL;
    }

    if ((now_ms - runtime->watchdog_started_ms) < AURORA_WATCHDOG_STARTUP_GRACE_MS)
    {
        runtime->watchdog_seen = 0U;
        runtime->watchdog_window_start_ms = now_ms;
        return;
    }

    if ((now_ms - runtime->watchdog_window_start_ms) >= AURORA_WATCHDOG_WINDOW_MS)
    {
        if ((runtime->watchdog_seen & required) == required)
        {
            /* 整个工程只有这里允许喂硬件看门狗。 */
            drv_watchdog_feed();
            DEBUG_WDG_PRINTF("feed tickets=0x%08lx",
                             (unsigned long)required);
        }
        runtime->watchdog_seen = 0U;
        runtime->watchdog_window_start_ms = now_ms;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_app_runtime_init(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : true表示全部关键模块初始化并启动成功，false表示保持安全失败态
 * Description : 按安全顺序初始化系统、GPIO、IWDT、PWM、比较器、ADC、UART、标定、应用和存储，最后启动ADC并发布应用运行时就绪。
 *---------------------------------------------------------------------------*/
bool aurora_app_runtime_init(aurora_app_runtime_t *runtime)
{
    aurora_measurement_calibration_t calibration;
    drv_board_adc_calibration_t board_cal;
    size_t channel;
    uint32_t now_ms;

    if (runtime == NULL)
    {
        return false;
    }
    memset(runtime, 0, sizeof(*runtime));

    drv_system_init();
    drv_irq_configure_priorities();
    drv_io_init();
    drv_io_set_relay(false);
    drv_io_set_link(false);
    drv_io_set_leds(false, false);

    /*
     * 安全GPIO建立后立即启动IWDT。后续任一外设初始化卡死或失败，
     * main中的安全等待将由看门狗复位，而不是永久停在未知状态。
     */
    now_ms = drv_time_now_ms();
    if (!drv_watchdog_init(AURORA_WATCHDOG_TIMEOUT_MS))
    {
        return false;
    }
    runtime->watchdog_started_ms = now_ms;
    runtime->watchdog_window_start_ms = now_ms;

    if (!drv_pwm_init())
    {
        return false;
    }
    force_safe_off(runtime);
    if (!drv_comp_init() || !drv_adc_init() || !drv_uart_init())
    {
        return false;
    }

    memset(&calibration, 0, sizeof(calibration));
    for (channel = 0U; channel < AURORA_ADC_CHANNEL_COUNT; ++channel)
    {
        if (drv_board_get_adc_calibration(channel, &board_cal))
        {
            calibration.channel[channel].gain_num = board_cal.gain_num;
            calibration.channel[channel].gain_den = board_cal.gain_den;
            calibration.channel[channel].offset = board_cal.offset;
            calibration.channel[channel].zero_code = board_cal.zero_code;
            calibration.channel[channel].polarity = board_cal.polarity;
            calibration.channel[channel].valid = board_cal.valid;
            calibration.channel[channel].kind =
                (board_cal.kind == DRV_ADC_CALIBRATION_NTC_BETA) ?
                    AURORA_ADC_CALIBRATION_NTC_BETA :
                    AURORA_ADC_CALIBRATION_LINEAR;
            calibration.channel[channel].ntc_pullup_ohm = board_cal.ntc_pullup_ohm;
            calibration.channel[channel].ntc_r25_ohm = board_cal.ntc_r25_ohm;
            calibration.channel[channel].ntc_beta_kelvin = board_cal.ntc_beta_kelvin;
            calibration.channel[channel].ntc_full_scale_code =
                board_cal.ntc_full_scale_code;
            calibration.channel[channel].ntc_reference_temp_dc =
                board_cal.ntc_reference_temp_dc;
            calibration.channel[channel].ntc_min_temp_dc = board_cal.ntc_min_temp_dc;
            calibration.channel[channel].ntc_max_temp_dc = board_cal.ntc_max_temp_dc;
        }
    }
    aurora_app_init(&runtime->app, &calibration, now_ms);
    load_storage(runtime);
    runtime->safety_epoch = 1U;
    runtime->last_telemetry_ms = now_ms;

    if (!drv_adc_start())
    {
        return false;
    }
    debug_init();
    DEBUG_SYSTEM_PRINTF("USART mode=%u", (unsigned)BOARD_USART_MODE);
#if (BOARD_USART_MODE == BOARD_USART_MODE_BLUETOOTH)
    DEBUG_BLE_PRINTF("transport=bluetooth");
#else
    DEBUG_SYSTEM_PRINTF("transport=debug");
#endif
    runtime->initialized = true;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_runtime_poll(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : 主循环调度入口：原子领取事件，优先处理快速故障，再处理ADC/通信/1 ms控制，随后落实功率命令、遥测、存储和看门狗。
 *---------------------------------------------------------------------------*/
void aurora_app_runtime_poll(aurora_app_runtime_t *runtime)
{
    uint32_t events;
    uint32_t now_ms;

    if ((runtime == NULL) || !runtime->initialized)
    {
        return;
    }

    now_ms = drv_time_now_ms();
    events = atomic_exchange_u32(&runtime->event_flags, 0U);
    runtime->watchdog_seen |= APP_WDG_TICKET_MAIN;

    if ((events & APP_EVENT_FAST_FAULT) != 0U)
    {
        const uint32_t faults = atomic_exchange_u32(&runtime->pending_fault_mask, 0U);
        aurora_app_on_fast_fault(&runtime->app, faults, now_ms);
        DEBUG_PROTECTION_PRINTF("fast_fault=0x%08lx",
                                (unsigned long)faults);
        force_safe_off(runtime);
    }
    if ((events & APP_EVENT_ADC) != 0U)
    {
        process_adc(runtime);
    }
    if ((events & APP_EVENT_UART_RX) != 0U)
    {
        process_uart(runtime, now_ms);
    }
    if ((events & APP_EVENT_TICK) != 0U)
    {
        /* 应用层依据now_ms自行合并迟到的节拍并限制能量补计。 */
        aurora_app_step_1ms(&runtime->app, now_ms);
        drv_io_set_leds(runtime->app.ui_output.led_run_on,
                        runtime->app.ui_output.led_fault_on);
        runtime->watchdog_seen |= APP_WDG_TICKET_CONTROL;
    }

    apply_power_command(runtime);

#if (BOARD_USART_MODE == BOARD_USART_MODE_BLUETOOTH)
    if ((now_ms - runtime->last_telemetry_ms) >= AURORA_TELEMETRY_PERIOD_MS)
    {
        aurora_protocol_frame_t telemetry;
        uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
        size_t wire_length;
        aurora_protocol_fill_telemetry(&telemetry,
                                       runtime->app.telemetry_message_id++,
                                       &runtime->app.sample,
                                       runtime->app.charger.state,
                                       runtime->app.protection.latched_mask,
                                       &runtime->app.storage.settings);
        wire_length = aurora_protocol_encode(&telemetry, wire, sizeof(wire));
        if (wire_length != 0U)
        {
            (void)drv_uart_send(wire, wire_length);
        }
        runtime->last_telemetry_ms = now_ms;
    }
#endif

    app_storage(runtime, now_ms);
    app_watchdog(runtime, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_runtime_isr_tick(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : SysTick桥接：递增系统时间并投递1 ms事件，不在ISR内运行控制算法。
 *---------------------------------------------------------------------------*/
void aurora_app_runtime_isr_tick(aurora_app_runtime_t *runtime)
{
    if (runtime != NULL)
    {
        drv_time_tick_isr();
        atomic_or_u32(&runtime->event_flags, APP_EVENT_TICK);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_runtime_isr_adc_block(aurora_app_runtime_t *runtime, uint8_t block_index)
 * Input       : runtime - 应用运行时上下文；block_index - DMA半缓冲索引
 * Output      : 无
 * Description : DMA桥接：发布完成半块和时间戳；检测同半块覆盖时锁存ADC overrun快速故障。
 *---------------------------------------------------------------------------*/
void aurora_app_runtime_isr_adc_block(aurora_app_runtime_t *runtime, uint8_t block_index)
{
    if ((runtime != NULL) && (block_index < 2U))
    {
        const uint8_t bit = (uint8_t)(1U << block_index);
        bool overrun = false;
        aurora_irq_state_t irq = drv_irq_save();
        if (((runtime->adc_completed_mask | runtime->adc_processing_mask) & bit) != 0U)
        {
            runtime->adc_overrun_count++;
            overrun = true;
        }
        else
        {
            runtime->adc_timestamp_ms[block_index] = drv_time_now_ms();
            runtime->adc_completed_mask |= bit;
        }
        drv_irq_restore(irq);
        if (overrun)
        {
            aurora_app_runtime_isr_fast_fault(runtime, AURORA_FAULT_ADC_OVERRUN);
        }
        else
        {
            atomic_or_u32(&runtime->event_flags, APP_EVENT_ADC);
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_runtime_isr_fast_fault(aurora_app_runtime_t *runtime, uint32_t fault_mask)
 * Input       : runtime - 应用运行时上下文；fault_mask - 故障位图
 * Output      : 无
 * Description : 快速故障统一入口：第一动作强制关PWM，随后递增安全epoch并投递故障位图。
 *---------------------------------------------------------------------------*/
void aurora_app_runtime_isr_fast_fault(aurora_app_runtime_t *runtime, uint32_t fault_mask)
{
    if (runtime != NULL)
    {
        /* 第一条指令路径必须是硬关波；事件通知只负责后续诊断。 */
        drv_pwm_force_off_isr();
        runtime->safety_epoch++;
        atomic_or_u32(&runtime->pending_fault_mask, fault_mask);
        atomic_or_u32(&runtime->event_flags, APP_EVENT_FAST_FAULT);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_runtime_isr_pwm_update(aurora_app_runtime_t *runtime)
 * Input       : runtime - 应用运行时上下文
 * Output      : 无
 * Description : ATMR UPDATE桥接：确认首次零CCR已由自然UEV装载。
 *---------------------------------------------------------------------------*/
void aurora_app_runtime_isr_pwm_update(aurora_app_runtime_t *runtime)
{
    if (runtime != NULL)
    {
        drv_pwm_update_isr_ack();
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_runtime_isr_uart_rx(aurora_app_runtime_t *runtime, uint8_t byte)
 * Input       : runtime - 应用运行时上下文；byte - 接收字节
 * Output      : 无
 * Description : USART桥接：把单字节放入应用运行时 RX环形缓冲；满时累计溢出计数。
 *---------------------------------------------------------------------------*/
void aurora_app_runtime_isr_uart_rx(aurora_app_runtime_t *runtime, uint8_t byte)
{
    uint16_t next;

    if (runtime == NULL)
    {
        return;
    }
    next = (uint16_t)((runtime->uart_head + 1U) % sizeof(runtime->uart_rx));
    if (next != runtime->uart_tail)
    {
        runtime->uart_rx[runtime->uart_head] = byte;
        runtime->uart_head = next;
        atomic_or_u32(&runtime->event_flags, APP_EVENT_UART_RX);
    }
    else
    {
        runtime->uart_rx_overrun_count++;
    }
}

#if !defined(AURORA_HOST_TEST)
/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 无（正常不返回）
 * Description : 系统入口：由应用运行时依次建立时钟、GPIO安全态、IWDT、PWM关断、COMP/ADC/UART、应用和Flash；
 *               初始化失败时保持安全等待，成功后循环领取事件并用WFI等待下一次中断。
 *---------------------------------------------------------------------------*/
int main(void)
{
    if (!aurora_app_runtime_init(&g_aurora_app_runtime))
    {
        /* 初始化失败保持安全态；看门狗若已启动会复位，否则停在这里等待调试。 */
        for (;;)
        {
            drv_wait_for_interrupt();
        }
    }

    for (;;)
    {
        aurora_app_runtime_poll(&g_aurora_app_runtime);
        drv_wait_for_interrupt();
    }
}
#endif

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_init(aurora_app_t *app,
 *               const aurora_measurement_calibration_t *calibration,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；calibration - 六通道测量标定；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 清零应用状态并按依赖顺序初始化测量、MPPT、保护、功率级、UI、协议、存储和充电器。
 *---------------------------------------------------------------------------*/
void aurora_app_init(aurora_app_t *app,
                     const aurora_measurement_calibration_t *calibration,
                     uint32_t now_ms)
{
    if (app == NULL)
    {
        return;
    }

    memset(app, 0, sizeof(*app));
    aurora_measurement_init(&app->measurement, calibration);
    aurora_mppt_init(&app->mppt);
    aurora_protection_init(&app->protection, now_ms);
    aurora_power_stage_init(&app->power_stage, now_ms);
    aurora_ui_init(&app->ui);
    aurora_protocol_init(&app->protocol);
    aurora_storage_init_defaults(&app->storage);
    aurora_charger_init(&app->charger,
                        app->storage.settings.chemistry,
                        app->storage.settings.pack,
                        now_ms);
    app->last_step_ms = now_ms;
    app->last_10ms = now_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_apply_settings(aurora_app_t *app,
 *               const aurora_persistent_settings_t *settings,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；settings - 已校验的持久化设置；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 应用电池档案并安全复位充电、MPPT和功率状态，禁止旧参数下的积分或Duty跨配置继续运行。
 *---------------------------------------------------------------------------*/
void aurora_app_apply_settings(aurora_app_t *app,
                               const aurora_persistent_settings_t *settings,
                               uint32_t now_ms)
{
    if ((app == NULL) || (settings == NULL) ||
        (settings->chemistry >= AURORA_CHEM_COUNT) ||
        (settings->pack >= AURORA_PACK_COUNT))
    {
        return;
    }

    app->storage.settings = *settings;
    aurora_charger_init(&app->charger,
                        settings->chemistry,
                        settings->pack,
                        now_ms);
    aurora_mppt_reset(&app->mppt);
    aurora_power_stage_init(&app->power_stage, now_ms);

    /* 立即撤销上一次控制周期产生的命令，应用运行时本轮即可执行关波。 */
    memset(&app->charge_output, 0, sizeof(app->charge_output));
    memset(&app->mppt_output, 0, sizeof(app->mppt_output));
    memset(&app->power_command, 0, sizeof(app->power_command));
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_adc_block(aurora_app_t *app,
 *               const uint16_t *raw, size_t word_count,
 *               uint32_t timestamp_ms)
 * Input       : app - 应用总上下文；raw - 完整ADC DMA块；word_count - 块内字数；
 *               timestamp_ms - DMA完成时间戳
 * Output      : 无
 * Description : 处理应用运行时发布的完整ADC块；仅在换算成功后更新应用层测量快照。
 *---------------------------------------------------------------------------*/
void aurora_app_on_adc_block(aurora_app_t *app,
                             const uint16_t *raw,
                             size_t word_count,
                             uint32_t timestamp_ms)
{
    if (app == NULL)
    {
        return;
    }

    if (aurora_measurement_process_block(&app->measurement,
                                         raw,
                                         word_count,
                                         timestamp_ms) == AURORA_STATUS_OK)
    {
        (void)aurora_measurement_read(&app->measurement, &app->sample);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_fast_fault(aurora_app_t *app,
 *               uint32_t fault_mask, uint32_t now_ms)
 * Input       : app - 应用总上下文；fault_mask - ISR快速故障位；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 把ISR事件纳入统一保护锁存；本函数只记录故障，不执行任何恢复或重新发波。
 *---------------------------------------------------------------------------*/
void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms)
{
    if (app != NULL)
    {
        aurora_protection_latch_fast_fault(&app->protection,
                                           fault_mask,
                                           now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms)
 * Input       : app - 应用总上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 1ms应用调度入口：累计能量、估算BAT_I、执行保护；每10ms更新充电、MPPT和UI，再生成功率命令。
 *---------------------------------------------------------------------------*/
void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms)
{
    bool weak_light;
    bool thermal_limited;
    bool external_limited;
    uint32_t charge_limit_mw;
    uint32_t elapsed_step_ms;

    if (app == NULL)
    {
        return;
    }

    elapsed_step_ms = now_ms - app->last_step_ms;
    if (elapsed_step_ms == 0U)
    {
        return;
    }

    /* 调试暂停不应把整段墙上时间错误计入发电量。 */
    if (elapsed_step_ms > AURORA_MAX_ELAPSED_STEP_MS)
    {
        elapsed_step_ms = AURORA_MAX_ELAPSED_STEP_MS;
    }
    app->last_step_ms = now_ms;

    (void)aurora_measurement_read(&app->measurement, &app->sample);

    /* 使用mW·ms整型累计，达到1Wh后再更新持久化计数。 */
    if (app->sample.pv_power_mw > 0)
    {
        app->energy_accumulator_mw_ms +=
            (uint64_t)(uint32_t)app->sample.pv_power_mw * elapsed_step_ms;
        while (app->energy_accumulator_mw_ms >= AURORA_ONE_WH_MW_MS)
        {
            app->energy_accumulator_mw_ms -= AURORA_ONE_WH_MW_MS;
            app->storage.settings.lifetime_energy_wh++;
            app->storage.settings.daily_energy_wh++;
            aurora_storage_mark_dirty(&app->storage, now_ms);
        }
    }

    /* BAT_I为估算量；继电器瞬态或断开时必须撤销有效标记。 */
    aurora_measurement_estimate_battery_current(
        &app->sample,
        estimate_efficiency_q15(&app->sample),
        app->power_stage.relay_closed,
        app->power_stage.state == AURORA_POWER_RELAY_SETTLE);

    aurora_protection_step(&app->protection,
                           &app->sample,
                           &app->charger.profile,
                           now_ms);

    if ((now_ms - app->last_10ms) >= AURORA_CONTROL_PERIOD_MS)
    {
        const uint32_t elapsed_control_ms = now_ms - app->last_10ms;

        weak_light = app->sample.pv_power_mw < AURORA_NO_SUN_RECOVER_MW;
        thermal_limited =
            ((app->sample.valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U) &&
            (app->sample.mos_temp_dC > AURORA_MOS_DERATE_TEMP_DC);

        app->charge_output = aurora_charger_step(&app->charger,
                                                 &app->sample,
                                                 weak_light,
                                                 thermal_limited,
                                                 now_ms);

        /* 允许功率取充电阶段、PV电流和BOM额定功率三者的最小值。 */
        charge_limit_mw = app->charge_output.power_limit_mw;
        if (app->sample.pv_voltage_mv > 0)
        {
            const uint32_t pv_current_power_limit_mw =
                (uint32_t)(((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
                            (uint32_t)AURORA_PV_CURRENT_LIMIT_MA) /
                           AURORA_MV_MA_PER_MW);

            if (pv_current_power_limit_mw < charge_limit_mw)
            {
                charge_limit_mw = pv_current_power_limit_mw;
            }
        }
        if (charge_limit_mw > AURORA_RATED_POWER_MW)
        {
            charge_limit_mw = AURORA_RATED_POWER_MW;
        }

        /* 已受外部包络限制时冻结MPPT参考搜索，避免错误追踪限幅后的工作点。 */
        external_limited =
            thermal_limited ||
            !app->charge_output.allow_charge ||
            ((charge_limit_mw > AURORA_MPPT_P_NOISE_MW) &&
             ((uint32_t)((app->sample.pv_power_mw > 0) ?
                             app->sample.pv_power_mw : 0) >=
              (charge_limit_mw - AURORA_MPPT_P_NOISE_MW)));

        app->mppt_output = aurora_mppt_step(&app->mppt,
                                            &app->sample,
                                            charge_limit_mw,
                                            external_limited,
                                            now_ms);
        app->ui_output = aurora_ui_step(&app->ui,
                                        app->power_stage.state,
                                        app->protection.latched_mask,
                                        elapsed_control_ms);
        app->last_10ms = now_ms;
    }

    /* 功率状态机每1ms运行，能够及时撤销旧的PWM/继电器命令。 */
    app->power_command =
        aurora_power_stage_step(&app->power_stage,
                                &app->sample,
                                &app->mppt_output,
                                &app->charge_output,
                                aurora_protection_is_safe(&app->protection),
                                now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_protocol_frame(aurora_app_t *app,
 *               const aurora_protocol_frame_t *frame,
 *               aurora_protocol_frame_t *response, bool *has_response,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；frame - 已校验请求帧；response - 应答帧输出；
 *               has_response - 应答有效标志；now_ms - 当前毫秒时间戳
 * Output      : 无；通过response和has_response返回可选应答
 * Description : 处理电池档案设置和能量复位命令；运行期改档案会立即撤销旧控制状态并重新预充。
 *---------------------------------------------------------------------------*/
void aurora_app_on_protocol_frame(aurora_app_t *app,
                                  const aurora_protocol_frame_t *frame,
                                  aurora_protocol_frame_t *response,
                                  bool *has_response,
                                  uint32_t now_ms)
{
    if ((app == NULL) || (frame == NULL) ||
        (response == NULL) || (has_response == NULL))
    {
        return;
    }

    *has_response = false;

    if ((frame->resource == AURORA_PROTOCOL_RESOURCE_SETTING) &&
        (frame->action == AURORA_PROTOCOL_ACTION_WRITE))
    {
        uint8_t result = AURORA_PROTOCOL_RESULT_INVALID;

        if ((frame->data_length == AURORA_PROTOCOL_SETTING_DATA_LENGTH) &&
            (frame->data[0] < AURORA_CHEM_COUNT) &&
            (frame->data[1] < AURORA_PACK_COUNT))
        {
            aurora_persistent_settings_t settings = app->storage.settings;

            settings.chemistry = (aurora_battery_chem_t)frame->data[0];
            settings.pack = (aurora_battery_pack_t)frame->data[1];
            settings.settings_revision++;
            aurora_app_apply_settings(app, &settings, now_ms);
            aurora_storage_mark_dirty(&app->storage, now_ms);
            result = AURORA_PROTOCOL_RESULT_OK;
        }

        memset(response, 0, sizeof(*response));
        response->action = AURORA_PROTOCOL_ACTION_RESPONSE;
        response->resource = frame->resource;
        response->message_id = frame->message_id;
        response->data_length = AURORA_PROTOCOL_RESULT_DATA_LENGTH;
        response->data[0] = result;
        *has_response = true;
    }
    else if ((frame->resource == AURORA_PROTOCOL_RESOURCE_RESET) &&
             (frame->action == AURORA_PROTOCOL_ACTION_WRITE))
    {
        app->storage.settings.lifetime_energy_wh = 0U;
        app->storage.settings.daily_energy_wh = 0U;
        app->energy_accumulator_mw_ms = 0U;
        aurora_storage_mark_dirty(&app->storage, now_ms);

        memset(response, 0, sizeof(*response));
        response->action = AURORA_PROTOCOL_ACTION_RESPONSE;
        response->resource = frame->resource;
        response->message_id = frame->message_id;
        response->data_length = AURORA_PROTOCOL_RESULT_DATA_LENGTH;
        response->data[0] = AURORA_PROTOCOL_RESULT_OK;
        *has_response = true;
    }
}
