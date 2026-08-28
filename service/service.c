#include "service.h"

#include "app_config.h"
#include "board.h"
#include "driver.h"
#include "debug.h"

#include <string.h>

/* ISR事件位：1ms节拍。 */
#define SERVICE_EVENT_TICK                          (1UL << 0)
/* ISR事件位：至少一个ADC DMA半块完成。 */
#define SERVICE_EVENT_ADC                           (1UL << 1)
/* ISR事件位：快速故障等待主循环锁存。 */
#define SERVICE_EVENT_FAST_FAULT                    (1UL << 2)
/* ISR事件位：UART RX环形缓冲中有待处理数据。 */
#define SERVICE_EVENT_UART_RX                       (1UL << 3)

/* 看门狗健康票据：主循环正常推进。 */
#define SERVICE_WDG_TICKET_MAIN                     (1UL << 0)
/* 看门狗健康票据：功率运行状态下ADC仍持续发布。 */
#define SERVICE_WDG_TICKET_ADC                      (1UL << 1)
/* 看门狗健康票据：1ms应用控制链正常执行。 */
#define SERVICE_WDG_TICKET_CONTROL                  (1UL << 2)

/* PWM授权状态：完全关闭。 */
#define SERVICE_PWM_ARM_OFF                         (0U)
/* PWM授权状态：等待零CCR在自然UPDATE边界装载。 */
#define SERVICE_PWM_ARM_WAIT_ZERO                   (1U)
/* PWM授权状态：硬件已通过复核并放行。 */
#define SERVICE_PWM_ARM_ACTIVE                      (2U)

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
 * Name        : static void force_safe_off(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 统一关闭PWM、暂存零占空比并复位Service发波授权状态。
 *---------------------------------------------------------------------------*/
static void force_safe_off(aurora_service_t *service)
{
    drv_pwm_disarm();
    (void)drv_pwm_stage_duty(0U, NULL);
    service->pwm_arm_state = SERVICE_PWM_ARM_OFF;
}

/*---------------------------------------------------------------------------*
 * Name        : static bool safety_still_clear(const aurora_service_t *service, uint32_t token)
 * Input       : service - Service上下文；token - 安全epoch快照
 * Output      : true表示epoch、软件保护、Break源/锁存和人工总门均保持安全
 * Description : 复核安全epoch、待处理故障、软件保护、硬件Break和人工功率门禁，防止旧授权跨故障继续使用。
 *---------------------------------------------------------------------------*/
static bool safety_still_clear(const aurora_service_t *service, uint32_t token)
{
    return (service->safety_epoch == token) &&
           (service->pending_fault_mask == 0U) &&
           aurora_protection_is_safe(&service->app.protection) &&
           !drv_pwm_break_source_active() &&
           !drv_pwm_break_latched() &&
           aurora_board_power_gate_open();
}

/*---------------------------------------------------------------------------*
 * Name        : static void apply_power_command(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 把应用层功率命令落实到继电器和PWM；首次发波先等待零CCR自然UEV，运行中只写shadow，任一复核失败立即关波。
 *---------------------------------------------------------------------------*/
static void apply_power_command(aurora_service_t *service)
{
    const aurora_power_command_t *command = &service->app.power_command;
    uint32_t token;
    aurora_irq_state_t irq;

    drv_io_set_relay(command->relay_enable);

    if (!command->pwm_enable || !aurora_protection_is_safe(&service->app.protection))
    {
        force_safe_off(service);
        return;
    }

    if (service->pwm_arm_state == SERVICE_PWM_ARM_OFF)
    {
        /* 首次放行前先写0到CCR预装载，并等待至少一个自然UEV。 */
        drv_pwm_disarm();
        if (drv_pwm_prepare_arm_zero(&service->pwm_zero_sequence))
        {
            service->pwm_arm_state = SERVICE_PWM_ARM_WAIT_ZERO;
        }
        return;
    }

    if (service->pwm_arm_state == SERVICE_PWM_ARM_WAIT_ZERO)
    {
        if (drv_pwm_applied_sequence() < service->pwm_zero_sequence)
        {
            return;
        }

        token = service->safety_epoch;
        irq = drv_irq_save();
        if (!safety_still_clear(service, token) || !drv_pwm_arm())
        {
            drv_irq_restore(irq);
            force_safe_off(service);
            return;
        }
        drv_irq_restore(irq);

        /* 退出临界区后再查一次；期间即使CPU中断被屏蔽，硬件Break仍可直接关波。 */
        if (!safety_still_clear(service, token) || !drv_pwm_output_active())
        {
            force_safe_off(service);
            return;
        }
        service->pwm_arm_state = SERVICE_PWM_ARM_ACTIVE;
        return;
    }

    token = service->safety_epoch;
    if (!safety_still_clear(service, token))
    {
        force_safe_off(service);
        return;
    }

    /* 仅写CCR shadow；drv_pwm_stage_duty禁止运行期软件UG。 */
    if (!drv_pwm_stage_duty(command->duty_q15, NULL) || !safety_still_clear(service, token))
    {
        force_safe_off(service);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_adc(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 原子领取DMA完成块并标记处理中，逐块完成物理量处理；同半块被DMA追上时由ISR锁存overrun。
 *---------------------------------------------------------------------------*/
static void process_adc(aurora_service_t *service)
{
    uint8_t mask;
    uint8_t index;
    aurora_irq_state_t irq;

    /*
     * 原子取得已完成块，同时标记为“主循环处理中”。DMA ISR若在处理期间
     * 再次完成同一半缓冲，必须报告overrun，不能静默覆盖正在读取的数据。
     */
    irq = drv_irq_save();
    mask = service->adc_completed_mask;
    service->adc_completed_mask = 0U;
    service->adc_processing_mask |= mask;
    drv_irq_restore(irq);

    for (index = 0U; index < 2U; ++index)
    {
        const uint8_t bit = (uint8_t)(1U << index);
        if ((mask & bit) != 0U)
        {
            const uint16_t *block = drv_adc_completed_block(index);
            if (block != NULL)
            {
                aurora_app_on_adc_block(&service->app,
                                        block,
                                        drv_adc_block_words(),
                                        service->adc_timestamp_ms[index]);
                if (service->app.sample.sequence == 1U)
                {
                    DEBUG_NTC_PRINTF("mos_valid=%u mos_temp_dc=%d",
                                     (unsigned)((service->app.sample.valid_mask &
                                                 AURORA_MEAS_VALID_MOS_TEMP) != 0U),
                                     (int)service->app.sample.mos_temp_dC);
                }
                service->watchdog_seen |= SERVICE_WDG_TICKET_ADC;
            }

            irq = drv_irq_save();
            service->adc_processing_mask &= (uint8_t)~bit;
            drv_irq_restore(irq);
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_uart(aurora_service_t *service, uint32_t now_ms)
 * Input       : service - Service上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 按主循环预算消费RX环形缓冲、推进协议解析并发送应答；余量未处理完时重新投递事件。
 *---------------------------------------------------------------------------*/
static void process_uart(aurora_service_t *service, uint32_t now_ms)
{
#if (BOARD_USART_MODE == BOARD_USART_MODE_DEBUG)
    (void)service;
    (void)now_ms;
    /* Debug路由仅输出日志，禁止把调试输入误送入产品协议解析器。 */
    return;
#else
    aurora_protocol_frame_t request;
    aurora_protocol_frame_t response;
    bool has_response;
    uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
    size_t wire_length;
    uint32_t budget = BOARD_UART_SERVICE_RX_BUDGET;

    while (budget > 0U)
    {
        uint8_t byte;
        aurora_irq_state_t irq = drv_irq_save();
        if (service->uart_tail == service->uart_head)
        {
            drv_irq_restore(irq);
            break;
        }
        byte = service->uart_rx[service->uart_tail];
        service->uart_tail = (uint16_t)((service->uart_tail + 1U) % sizeof(service->uart_rx));
        drv_irq_restore(irq);
        budget--;

        aurora_protocol_feed_byte(&service->app.protocol, byte, now_ms);

        /* 每完成一帧立即取走，避免同一批RX数据中的后一帧覆盖前一帧。 */
        if (aurora_protocol_take_frame(&service->app.protocol, &request))
        {
            aurora_app_on_protocol_frame(&service->app,
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
                                 (unsigned)service->app.storage.settings.chemistry,
                                 (unsigned)service->app.storage.settings.pack);
            }
        }
    }

    /* 尚有字节时重新投递事件，避免一次主循环被通信长帧长期占用。 */
    if (service->uart_tail != service->uart_head)
    {
        atomic_or_u32(&service->event_flags, SERVICE_EVENT_UART_RX);
    }
#endif
}

/*---------------------------------------------------------------------------*
 * Name        : static void load_storage(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 读取并校验Flash A/B页，按回绕安全序号选择较新记录并应用到应用层。
 *---------------------------------------------------------------------------*/
static void load_storage(aurora_service_t *service)
{
    uint8_t page_a[AURORA_STORAGE_PAGE_SIZE];
    uint8_t page_b[AURORA_STORAGE_PAGE_SIZE];
    aurora_persistent_settings_t settings_a = {0};
    aurora_persistent_settings_t settings_b = {0};
    uint32_t seq_a = 0U;
    uint32_t seq_b = 0U;
    bool valid_a;
    bool valid_b;

    valid_a = drv_flash_read(aurora_board_flash_page_a(), page_a, sizeof(page_a)) &&
              aurora_storage_decode_page(page_a, sizeof(page_a), &settings_a, &seq_a);
    valid_b = drv_flash_read(aurora_board_flash_page_b(), page_b, sizeof(page_b)) &&
              aurora_storage_decode_page(page_b, sizeof(page_b), &settings_b, &seq_b);

    if (valid_a || valid_b)
    {
        if (valid_b && (!valid_a || ((int32_t)(seq_b - seq_a) > 0)))
        {
            service->app.storage.settings = settings_b;
            service->app.storage.sequence = seq_b;
        }
        else
        {
            service->app.storage.settings = settings_a;
            service->app.storage.sequence = seq_a;
        }
        aurora_app_apply_settings(&service->app, &service->app.storage.settings, drv_time_now_ms());
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void service_storage(aurora_service_t *service, uint32_t now_ms)
 * Input       : service - Service上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 在脏数据稳定、PWM关闭且继电器断开时执行双页Journal保存，并把Commit Marker作为最后一步写入。
 *---------------------------------------------------------------------------*/
static void service_storage(aurora_service_t *service, uint32_t now_ms)
{
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t target;
    size_t used;

    if (!service->app.storage.dirty ||
        ((now_ms - service->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        drv_pwm_output_active() || service->app.power_stage.relay_closed)
    {
        return;
    }

    service->app.storage.sequence++;
    target = ((service->app.storage.sequence & 1U) != 0U) ?
                 aurora_board_flash_page_a() : aurora_board_flash_page_b();
    used = aurora_storage_encode_page(&service->app.storage, page, sizeof(page), false);
    if ((used < AURORA_STORAGE_HEADER_SIZE) || !drv_flash_erase_page(target) ||
        !drv_flash_program(target, page, AURORA_STORAGE_COMMIT_OFFSET) ||
        !drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t),
                           &page[AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t)],
                           used - AURORA_STORAGE_COMMIT_OFFSET - sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&service->app.protection,
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
        aurora_protection_latch_fast_fault(&service->app.protection,
                                           AURORA_FAULT_STORAGE,
                                           now_ms);
        DEBUG_STORAGE_PRINTF("commit_failed address=0x%08lx",
                             (unsigned long)target);
        return;
    }
    service->app.storage.dirty = false;
    DEBUG_STORAGE_PRINTF("saved sequence=%lu address=0x%08lx",
                         (unsigned long)service->app.storage.sequence,
                         (unsigned long)target);
}

/*---------------------------------------------------------------------------*
 * Name        : static void service_watchdog(aurora_service_t *service, uint32_t now_ms)
 * Input       : service - Service上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 按时间窗核对主循环、控制和功率状态下的ADC心跳；只有全部票据齐全时才喂IWDT。
 *---------------------------------------------------------------------------*/
static void service_watchdog(aurora_service_t *service, uint32_t now_ms)
{
    uint32_t required = SERVICE_WDG_TICKET_MAIN | SERVICE_WDG_TICKET_CONTROL;

    if ((service->app.power_stage.state == AURORA_POWER_PRECHARGE) ||
        (service->app.power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (service->app.power_stage.state == AURORA_POWER_RUN))
    {
        required |= SERVICE_WDG_TICKET_ADC | SERVICE_WDG_TICKET_CONTROL;
    }

    if ((now_ms - service->watchdog_started_ms) < AURORA_WATCHDOG_STARTUP_GRACE_MS)
    {
        service->watchdog_seen = 0U;
        service->watchdog_window_start_ms = now_ms;
        return;
    }

    if ((now_ms - service->watchdog_window_start_ms) >= AURORA_WATCHDOG_WINDOW_MS)
    {
        if ((service->watchdog_seen & required) == required)
        {
            /* 整个工程只有这里允许喂硬件看门狗。 */
            drv_watchdog_feed();
            DEBUG_WDG_PRINTF("feed tickets=0x%08lx",
                             (unsigned long)required);
        }
        service->watchdog_seen = 0U;
        service->watchdog_window_start_ms = now_ms;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_service_init(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : true表示全部关键模块初始化并启动成功，false表示保持安全失败态
 * Description : 按安全顺序初始化系统、GPIO、IWDT、PWM、比较器、ADC、UART、标定、应用和存储，最后启动ADC并发布Service就绪。
 *---------------------------------------------------------------------------*/
bool aurora_service_init(aurora_service_t *service)
{
    aurora_measurement_calibration_t calibration;
    aurora_board_adc_calibration_t board_cal;
    size_t channel;
    uint32_t now_ms;

    if (service == NULL)
    {
        return false;
    }
    memset(service, 0, sizeof(*service));

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
    service->watchdog_started_ms = now_ms;
    service->watchdog_window_start_ms = now_ms;

    if (!drv_pwm_init())
    {
        return false;
    }
    force_safe_off(service);
    if (!drv_comp_init() || !drv_adc_init() || !drv_uart_init())
    {
        return false;
    }

    memset(&calibration, 0, sizeof(calibration));
    for (channel = 0U; channel < AURORA_ADC_CHANNEL_COUNT; ++channel)
    {
        if (aurora_board_get_adc_calibration(channel, &board_cal))
        {
            calibration.channel[channel].gain_num = board_cal.gain_num;
            calibration.channel[channel].gain_den = board_cal.gain_den;
            calibration.channel[channel].offset = board_cal.offset;
            calibration.channel[channel].zero_code = board_cal.zero_code;
            calibration.channel[channel].polarity = board_cal.polarity;
            calibration.channel[channel].valid = board_cal.valid;
            calibration.channel[channel].kind = board_cal.kind;
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
    aurora_app_init(&service->app, &calibration, now_ms);
    load_storage(service);
    service->safety_epoch = 1U;
    service->last_telemetry_ms = now_ms;

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
    service->initialized = true;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_poll(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 主循环调度入口：原子领取事件，优先处理快速故障，再处理ADC/通信/1 ms控制，随后落实功率命令、遥测、存储和看门狗。
 *---------------------------------------------------------------------------*/
void aurora_service_poll(aurora_service_t *service)
{
    uint32_t events;
    uint32_t now_ms;

    if ((service == NULL) || !service->initialized)
    {
        return;
    }

    now_ms = drv_time_now_ms();
    events = atomic_exchange_u32(&service->event_flags, 0U);
    service->watchdog_seen |= SERVICE_WDG_TICKET_MAIN;

    if ((events & SERVICE_EVENT_FAST_FAULT) != 0U)
    {
        const uint32_t faults = atomic_exchange_u32(&service->pending_fault_mask, 0U);
        aurora_app_on_fast_fault(&service->app, faults, now_ms);
        DEBUG_PROTECTION_PRINTF("fast_fault=0x%08lx",
                                (unsigned long)faults);
        force_safe_off(service);
    }
    if ((events & SERVICE_EVENT_ADC) != 0U)
    {
        process_adc(service);
    }
    if ((events & SERVICE_EVENT_UART_RX) != 0U)
    {
        process_uart(service, now_ms);
    }
    if ((events & SERVICE_EVENT_TICK) != 0U)
    {
        /* 应用层依据now_ms自行合并迟到的节拍并限制能量补计。 */
        aurora_app_step_1ms(&service->app, now_ms);
        drv_io_set_leds(service->app.ui_output.led_run_on,
                        service->app.ui_output.led_fault_on);
        service->watchdog_seen |= SERVICE_WDG_TICKET_CONTROL;
    }

    apply_power_command(service);

#if (BOARD_USART_MODE == BOARD_USART_MODE_BLUETOOTH)
    if ((now_ms - service->last_telemetry_ms) >= AURORA_TELEMETRY_PERIOD_MS)
    {
        aurora_protocol_frame_t telemetry;
        uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
        size_t wire_length;
        aurora_protocol_fill_telemetry(&telemetry,
                                       service->app.telemetry_message_id++,
                                       &service->app.sample,
                                       service->app.charger.state,
                                       service->app.protection.latched_mask,
                                       &service->app.storage.settings);
        wire_length = aurora_protocol_encode(&telemetry, wire, sizeof(wire));
        if (wire_length != 0U)
        {
            (void)drv_uart_send(wire, wire_length);
        }
        service->last_telemetry_ms = now_ms;
    }
#endif

    service_storage(service, now_ms);
    service_watchdog(service, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_tick(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : SysTick桥接：递增系统时间并投递1 ms事件，不在ISR内运行控制算法。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_tick(aurora_service_t *service)
{
    if (service != NULL)
    {
        drv_time_tick_isr();
        atomic_or_u32(&service->event_flags, SERVICE_EVENT_TICK);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_adc_block(aurora_service_t *service, uint8_t block_index)
 * Input       : service - Service上下文；block_index - DMA半缓冲索引
 * Output      : 无
 * Description : DMA桥接：发布完成半块和时间戳；检测同半块覆盖时锁存ADC overrun快速故障。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_adc_block(aurora_service_t *service, uint8_t block_index)
{
    if ((service != NULL) && (block_index < 2U))
    {
        const uint8_t bit = (uint8_t)(1U << block_index);
        bool overrun = false;
        aurora_irq_state_t irq = drv_irq_save();
        if (((service->adc_completed_mask | service->adc_processing_mask) & bit) != 0U)
        {
            service->adc_overrun_count++;
            overrun = true;
        }
        else
        {
            service->adc_timestamp_ms[block_index] = drv_time_now_ms();
            service->adc_completed_mask |= bit;
        }
        drv_irq_restore(irq);
        if (overrun)
        {
            aurora_service_isr_fast_fault(service, AURORA_FAULT_ADC_OVERRUN);
        }
        else
        {
            atomic_or_u32(&service->event_flags, SERVICE_EVENT_ADC);
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_fast_fault(aurora_service_t *service, uint32_t fault_mask)
 * Input       : service - Service上下文；fault_mask - 故障位图
 * Output      : 无
 * Description : 快速故障统一入口：第一动作强制关PWM，随后递增安全epoch并投递故障位图。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_fast_fault(aurora_service_t *service, uint32_t fault_mask)
{
    if (service != NULL)
    {
        /* 第一条指令路径必须是硬关波；事件通知只负责后续诊断。 */
        drv_pwm_force_off_isr();
        service->safety_epoch++;
        atomic_or_u32(&service->pending_fault_mask, fault_mask);
        atomic_or_u32(&service->event_flags, SERVICE_EVENT_FAST_FAULT);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_pwm_update(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : ATMR UPDATE桥接：确认首次零CCR已由自然UEV装载。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_pwm_update(aurora_service_t *service)
{
    if (service != NULL)
    {
        drv_pwm_update_isr_ack();
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_uart_rx(aurora_service_t *service, uint8_t byte)
 * Input       : service - Service上下文；byte - 接收字节
 * Output      : 无
 * Description : USART桥接：把单字节放入Service RX环形缓冲；满时累计溢出计数。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_uart_rx(aurora_service_t *service, uint8_t byte)
{
    uint16_t next;

    if (service == NULL)
    {
        return;
    }
    next = (uint16_t)((service->uart_head + 1U) % sizeof(service->uart_rx));
    if (next != service->uart_tail)
    {
        service->uart_rx[service->uart_head] = byte;
        service->uart_head = next;
        atomic_or_u32(&service->event_flags, SERVICE_EVENT_UART_RX);
    }
    else
    {
        service->uart_rx_overrun_count++;
    }
}
