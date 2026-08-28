#include "service.h"

#include "app_config.h"
#include "board.h"
#include "driver.h"

#include <string.h>

/* ISR事件位：1ms节拍。 */
#define SERVICE_EVENT_TICK                          (1UL << 0)
/* ISR事件位：至少一个ADC DMA半块完成。 */
#define SERVICE_EVENT_ADC                           (1UL << 1)
/* ISR事件位：快速故障等待主循环锁存。 */
#define SERVICE_EVENT_FAST_FAULT                    (1UL << 2)
/* ISR事件位：UART RX环形缓冲中有待处理数据。 */
#define SERVICE_EVENT_UART_RX                       (1UL << 3)

/* 看门狗健康票据。 */
#define SERVICE_WDG_TICKET_MAIN                     (1UL << 0)
#define SERVICE_WDG_TICKET_ADC                      (1UL << 1)
#define SERVICE_WDG_TICKET_CONTROL                  (1UL << 2)

/* PWM授权状态。 */
#define SERVICE_PWM_ARM_OFF                         (0U)
#define SERVICE_PWM_ARM_WAIT_ZERO                   (1U)
#define SERVICE_PWM_ARM_ACTIVE                      (2U)

/* 可在硬件源消失30s后重新启动的快速比较器故障集合。 */
#define SERVICE_FAST_OCP_MASK                       (AURORA_FAULT_FAST_MOS_OCP | \
                                                     AURORA_FAULT_FAST_PV_OCP | \
                                                     AURORA_FAULT_FAST_BREAK)

/*---------------------------------------------------------------------------*
 * Name        : static void atomic_or_u32(volatile uint32_t *target, uint32_t value)
 * Input       : target - 目标值；value - 待合并位
 * Output      : 无
 * Description : 在短PRIMASK临界区内原子OR ISR/主循环共享位图。
 *---------------------------------------------------------------------------*/
static void atomic_or_u32(volatile uint32_t *target, uint32_t value)
{
    aurora_irq_state_t irq = drv_irq_save();
    *target |= value;
    drv_irq_restore(irq);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t atomic_exchange_u32(volatile uint32_t *target,
 *               uint32_t value)
 * Input       : target - 目标值；value - 替换值
 * Output      : 替换前32位值
 * Description : 原子领取事件/故障位，避免ISR置位与主循环清零发生丢事件竞态。
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
 * Name        : static bool relay_close_still_safe(const aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : true表示当前这一刻仍允许物理吸合继电器
 * Description : 在APP完成1s压差确认后，Service真正写GPIO前再次检查最新BST_U/BAT_U、
 *               软件/ISR故障和PWM实际状态，防止APP计算到GPIO动作之间的TOCTOU窗口。
 *---------------------------------------------------------------------------*/
static bool relay_close_still_safe(const aurora_service_t *service)
{
    const aurora_measurement_t *sample = &service->app.sample;
    const uint32_t required = AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    int64_t delta_mv;

    if ((service->app.power_command.state != AURORA_POWER_RELAY_SETTLE) ||
        service->app.power_command.pwm_enable ||
        (service->pending_fault_mask != 0U) ||
        !aurora_protection_is_safe(&service->app.protection) ||
        drv_pwm_output_active() ||
        ((sample->valid_mask & required) != required))
    {
        return false;
    }

    delta_mv = (int64_t)sample->bus_voltage_mv - sample->battery_voltage_mv;
    if (delta_mv < 0LL)
    {
        delta_mv = -delta_mv;
    }
    return delta_mv <= AURORA_RELAY_CLOSE_DELTA_MV;
}

/*---------------------------------------------------------------------------*
 * Name        : static void force_safe_off(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 统一物理关PWM、暂存0 Duty并撤销Service发波授权；不自动清Break锁存。
 *---------------------------------------------------------------------------*/
static void force_safe_off(aurora_service_t *service)
{
    drv_pwm_disarm();
    (void)drv_pwm_stage_duty(0U, NULL);
    service->pwm_arm_state = SERVICE_PWM_ARM_OFF;
}

/*---------------------------------------------------------------------------*
 * Name        : static bool safety_still_clear(const aurora_service_t *service,
 *               uint32_t token)
 * Input       : service - Service上下文；token - 安全epoch快照
 * Output      : true表示软件/硬件/人工门禁均仍安全
 * Description : 在真正写MOE前后复核epoch、故障、Break和总门，阻断故障ISR后低优先级代码重新发波。
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
 * Name        : static void clear_startup_break_if_safe(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : PWM从未输出时CMP只保留诊断；硬件源已消失且无快速OCP锁存时，主循环显式清理遗留Break以允许后续正常arm。
 *---------------------------------------------------------------------------*/
static void clear_startup_break_if_safe(aurora_service_t *service)
{
    const uint32_t faults = aurora_protection_fault_mask(&service->app.protection);

    if (!drv_pwm_output_active() && drv_pwm_break_latched() &&
        !drv_pwm_break_source_active() &&
        ((faults & SERVICE_FAST_OCP_MASK) == 0U))
    {
        (void)drv_pwm_clear_break_latch();
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void service_fast_ocp_recovery(aurora_service_t *service,
 *               uint32_t now_ms)
 * Input       : service - Service上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 真正运行阶段的CMP OCP需硬件源连续消失30s，PWM保持关闭后才清Break和软件锁存，之后仍重新走启动/预充。
 *---------------------------------------------------------------------------*/
static void service_fast_ocp_recovery(aurora_service_t *service, uint32_t now_ms)
{
    const uint32_t fault_mask = aurora_protection_fault_mask(&service->app.protection);

    if ((fault_mask & SERVICE_FAST_OCP_MASK) == 0U)
    {
        service->fast_ocp_recover_since_ms = 0U;
        return;
    }
    if (drv_pwm_break_source_active())
    {
        service->fast_ocp_recover_since_ms = 0U;
        return;
    }
    if (service->fast_ocp_recover_since_ms == 0U)
    {
        service->fast_ocp_recover_since_ms = now_ms;
        return;
    }
    if ((now_ms - service->fast_ocp_recover_since_ms) < AURORA_FAST_OCP_RECOVER_DELAY_MS)
    {
        return;
    }

    force_safe_off(service);
    if (drv_pwm_clear_break_latch() &&
        aurora_protection_clear_verified_fast_fault(&service->app.protection,
                                                     (uint32_t)SERVICE_FAST_OCP_MASK,
                                                     true))
    {
        /* Protection内部负责active/latched清理；Service只推进物理安全epoch。 */
        service->safety_epoch++;
        service->fast_ocp_recover_since_ms = 0U;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void apply_power_command(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 严格按“先关PWM再切继电器；首次PWM先0CCR自然UEV；最终多次安全复核”落实应用命令。
 *---------------------------------------------------------------------------*/
static void apply_power_command(aurora_service_t *service)
{
    const aurora_power_command_t *command = &service->app.power_command;
    uint32_t token;
    aurora_irq_state_t irq;

    /*
     * 继电器状态变化必须先物理关PWM。
     * 尤其PRECHARGE→RELAY_SETTLE：BST_U已先充到接近BAT_U，随后先关PWM，再真正吸合继电器。
     */
    if (command->relay_enable != service->relay_applied)
    {
        /* 任何继电器切换先撤销物理PWM；断开无条件允许，闭合必须再做一次实时安全复核。 */
        force_safe_off(service);

        if (command->relay_enable && !relay_close_still_safe(service))
        {
            drv_io_set_relay(false);
            service->relay_applied = false;
            return;
        }

        drv_io_set_relay(command->relay_enable);
        service->relay_applied = command->relay_enable;
        return;
    }

    if (!command->pwm_enable || !aurora_protection_is_safe(&service->app.protection))
    {
        force_safe_off(service);
        return;
    }

    /* PWM未输出阶段的瞬态CMP可能留下Break；只在无软件快速故障且实时源已消失时清除。 */
    clear_startup_break_if_safe(service);

    if (service->pwm_arm_state == SERVICE_PWM_ARM_OFF)
    {
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

    /* 只写CCR shadow；运行期禁止软件UG，Duty必须在下一自然UPDATE边界生效。 */
    if (!drv_pwm_stage_duty(command->duty_q15, NULL) ||
        !safety_still_clear(service, token))
    {
        force_safe_off(service);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_adc(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 原子领取DMA完成块并标记处理中；同半块被DMA追上时ISR锁存overrun。
 *---------------------------------------------------------------------------*/
static void process_adc(aurora_service_t *service)
{
    uint8_t mask;
    uint8_t index;
    aurora_irq_state_t irq;

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
 * Input       : service - Service上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按预算消费RX环形缓冲、推进协议解析并发送应答，避免通信长期占用主循环。
 *---------------------------------------------------------------------------*/
static void process_uart(aurora_service_t *service, uint32_t now_ms)
{
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
        }
    }

    if (service->uart_tail != service->uart_head)
    {
        atomic_or_u32(&service->event_flags, SERVICE_EVENT_UART_RX);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void load_storage(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 读取Flash A/B页，按回绕安全序号选择最新有效记录并重新初始化充电启动链。
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
        aurora_app_apply_settings(&service->app,
                                  &service->app.storage.settings,
                                  drv_time_now_ms());
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void service_storage(aurora_service_t *service, uint32_t now_ms)
 * Input       : service - Service上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 只有PWM关闭且物理继电器断开时执行双页Journal保存，Commit Marker最后写入。
 *---------------------------------------------------------------------------*/
static void service_storage(aurora_service_t *service, uint32_t now_ms)
{
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t target;
    size_t used;

    if (!service->app.storage.dirty ||
        ((now_ms - service->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        drv_pwm_output_active() || service->relay_applied)
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
                                           AURORA_FAULT_STORAGE, now_ms);
        return;
    }

    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET,
                           &((uint32_t){AURORA_STORAGE_COMMIT_MARKER}),
                           sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&service->app.protection,
                                           AURORA_FAULT_STORAGE, now_ms);
        return;
    }
    service->app.storage.dirty = false;
}

/*---------------------------------------------------------------------------*
 * Name        : static void service_watchdog(aurora_service_t *service, uint32_t now_ms)
 * Input       : service - Service上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按健康窗口核对主循环、控制和需要ADC的启动/功率状态；只有票据齐全才喂IWDT。
 *---------------------------------------------------------------------------*/
static void service_watchdog(aurora_service_t *service, uint32_t now_ms)
{
    uint32_t required = SERVICE_WDG_TICKET_MAIN | SERVICE_WDG_TICKET_CONTROL;

    if ((service->app.power_stage.state == AURORA_POWER_ZERO_CAL) ||
        (service->app.power_stage.state == AURORA_POWER_PRECHARGE) ||
        (service->app.power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (service->app.power_stage.state == AURORA_POWER_BAT_STABILITY) ||
        (service->app.power_stage.state == AURORA_POWER_RUN))
    {
        required |= SERVICE_WDG_TICKET_ADC;
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
            drv_watchdog_feed();
        }
        service->watchdog_seen = 0U;
        service->watchdog_window_start_ms = now_ms;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_service_init(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : true表示全部关键模块初始化成功
 * Description : 按安全顺序建立GPIO/IWDT/PWM/COMP/ADC/UART和APP，最后才启动定时触发ADC流。
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
    service->relay_applied = false;

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
    service->initialized = true;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_poll(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 主循环优先消费快速故障，再处理ADC/UART/控制，最后执行恢复、功率命令、遥测、存储和看门狗。
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
        aurora_app_step_1ms(&service->app, now_ms, drv_pwm_output_active());
        drv_io_set_leds(service->app.ui_output.led_run_on,
                        service->app.ui_output.led_fault_on);
        drv_io_set_link(service->app.link_request);
        service->watchdog_seen |= SERVICE_WDG_TICKET_CONTROL;
    }

    service_fast_ocp_recovery(service, now_ms);
    apply_power_command(service);

    if ((now_ms - service->last_telemetry_ms) >= AURORA_TELEMETRY_PERIOD_MS)
    {
        aurora_protocol_frame_t telemetry;
        uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
        size_t wire_length;

        aurora_protocol_fill_telemetry(&telemetry,
                                       service->app.telemetry_message_id++,
                                       &service->app.sample,
                                       service->app.charger.state,
                                       aurora_protection_fault_mask(&service->app.protection),
                                       &service->app.storage.settings);
        wire_length = aurora_protocol_encode(&telemetry, wire, sizeof(wire));
        if (wire_length != 0U)
        {
            (void)drv_uart_send(wire, wire_length);
        }
        service->last_telemetry_ms = now_ms;
    }

    service_storage(service, now_ms);
    service_watchdog(service, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_tick(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : SysTick只递增时间并投递1ms事件，不在ISR执行控制算法。
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
 * Name        : void aurora_service_isr_adc_block(aurora_service_t *service,
 *               uint8_t block_index)
 * Input       : service - Service上下文；block_index - DMA半缓冲索引
 * Output      : 无
 * Description : 发布完整DMA半块；同一半块覆盖正在处理的数据时锁存ADC_OVERRUN。
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
 * Name        : void aurora_service_isr_fast_fault(aurora_service_t *service,
 *               uint32_t fault_mask)
 * Input       : service - Service上下文；fault_mask - 快速故障位
 * Output      : 无
 * Description : 通用快速故障第一动作物理关PWM，随后递增epoch并投递主循环锁存。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_fast_fault(aurora_service_t *service, uint32_t fault_mask)
{
    if (service != NULL)
    {
        drv_pwm_force_off_isr();
        service->safety_epoch++;
        atomic_or_u32(&service->pending_fault_mask, fault_mask);
        atomic_or_u32(&service->event_flags, SERVICE_EVENT_FAST_FAULT);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_comparator_fault(
 *               aurora_service_t *service, uint32_t fault_mask)
 * Input       : service - Service上下文；fault_mask - COMP映射后的OCP故障位
 * Output      : 无
 * Description : V1.12.19适配：硬件始终可关波；若PWM实际未输出只记录诊断，不软件锁存OCP；运行中则走快速故障链。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_comparator_fault(aurora_service_t *service, uint32_t fault_mask)
{
    bool pwm_was_active;

    if (service == NULL)
    {
        return;
    }

    pwm_was_active = drv_pwm_output_active();
    drv_pwm_force_off_isr();
    if (!pwm_was_active)
    {
        service->startup_comp_ignored_count++;
        return;
    }

    service->safety_epoch++;
    atomic_or_u32(&service->pending_fault_mask, fault_mask);
    atomic_or_u32(&service->event_flags, SERVICE_EVENT_FAST_FAULT);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_pwm_update(aurora_service_t *service)
 * Input       : service - Service上下文
 * Output      : 无
 * Description : 确认首次0 CCR已由自然UPDATE装载，不在UPDATE ISR内重新开PWM。
 *---------------------------------------------------------------------------*/
void aurora_service_isr_pwm_update(aurora_service_t *service)
{
    if (service != NULL)
    {
        drv_pwm_update_isr_ack();
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_service_isr_uart_rx(aurora_service_t *service,
 *               uint8_t byte)
 * Input       : service - Service上下文；byte - 接收字节
 * Output      : 无
 * Description : ISR只把字节写入固定环形缓冲，满时累计溢出，不解析完整协议帧。
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
