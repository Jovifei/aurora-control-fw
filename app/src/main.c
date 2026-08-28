#include "main.h"

#include "debug.h"
#include "driver.h"

#include <string.h>

/* 弱光判定仅用于暂停尾流和MPPT搜索，不等价于13V无太阳关机条件。 */
#define APP_WEAK_LIGHT_POWER_MW                    (3000L)

/* ISR事件位：1ms节拍。 */
#define RUNTIME_EVENT_TICK                          (1UL << 0)
/* ISR事件位：至少一个ADC DMA半块完成。 */
#define RUNTIME_EVENT_ADC                           (1UL << 1)
/* ISR事件位：快速故障等待主循环锁存。 */
#define RUNTIME_EVENT_FAST_FAULT                    (1UL << 2)
/* ISR事件位：UART RX环形缓冲中有待处理数据。 */
#define RUNTIME_EVENT_UART_RX                       (1UL << 3)

/* 看门狗健康票据。 */
#define RUNTIME_WDG_TICKET_MAIN                     (1UL << 0)
#define RUNTIME_WDG_TICKET_ADC                      (1UL << 1)
#define RUNTIME_WDG_TICKET_CONTROL                  (1UL << 2)

/* PWM授权状态。 */
#define RUNTIME_PWM_ARM_OFF                         (0U)
#define RUNTIME_PWM_ARM_WAIT_ZERO                   (1U)
#define RUNTIME_PWM_ARM_ACTIVE                      (2U)

/* 可在硬件源消失30s后重新启动的快速比较器故障集合。 */
#define RUNTIME_FAST_OCP_MASK                       (AURORA_FAULT_FAST_MOS_OCP | \
                                                     AURORA_FAULT_FAST_PV_OCP | \
                                                     AURORA_FAULT_FAST_BREAK)

/* 目标中断桥接与主循环共享的唯一运行实例。 */
aurora_runtime_t g_aurora_runtime;

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t min_u32(uint32_t a, uint32_t b)
 * Input       : a/b - 两个无符号32位值
 * Output      : 较小值
 * Description : 组合电池、输入电流、低输入电压、温度和额定功率包络时统一取更严格上限。
 *---------------------------------------------------------------------------*/
static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t estimate_efficiency_q15(
 *               const aurora_measurement_t *sample)
 * Input       : sample - 最新测量快照
 * Output      : 当前功率区间的保守效率估计，Q15
 * Description : 无BAT_I通道时按PV功率分段估算效率；实测效率图确认后只调整集中参数。
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
 * Name        : static uint32_t input_voltage_power_limit_mw(int32_t pv_voltage_mv)
 * Input       : pv_voltage_mv - PV电压，mV
 * Output      : 低输入电压包络允许的最大PV功率，mW
 * Description : 继承V2.7：<=12V为50W，>=17V为当前BOM额定功率，中间线性插值。
 *---------------------------------------------------------------------------*/
static uint32_t input_voltage_power_limit_mw(int32_t pv_voltage_mv)
{
    const uint32_t floor_mw = min_u32(AURORA_LOW_PV_POWER_FLOOR_MW,
                                      AURORA_RATED_POWER_MW);

    if (pv_voltage_mv <= AURORA_LOW_PV_POWER_START_MV)
    {
        return floor_mw;
    }
    if (pv_voltage_mv >= AURORA_LOW_PV_POWER_FULL_MV)
    {
        return AURORA_RATED_POWER_MW;
    }

    return floor_mw +
           (uint32_t)(((uint64_t)(uint32_t)(pv_voltage_mv - AURORA_LOW_PV_POWER_START_MV) *
                       (AURORA_RATED_POWER_MW - floor_mw)) /
                      (uint32_t)(AURORA_LOW_PV_POWER_FULL_MV -
                                 AURORA_LOW_PV_POWER_START_MV));
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t thermal_power_limit_mw(
 *               const aurora_measurement_t *sample)
 * Input       : sample - 最新测量快照
 * Output      : 当前MOS温度允许的PV功率，mW
 * Description : 95°C开始降额，104°C降到候选最小功率；105°C保护由Protection独立处理。
 *---------------------------------------------------------------------------*/
static uint32_t thermal_power_limit_mw(const aurora_measurement_t *sample)
{
    if ((sample->valid_mask & AURORA_MEAS_VALID_MOS_TEMP) == 0U)
    {
        return AURORA_RATED_POWER_MW;
    }
    if (sample->mos_temp_dC <= AURORA_MOS_DERATE_TEMP_DC)
    {
        return AURORA_RATED_POWER_MW;
    }
    if (sample->mos_temp_dC >= AURORA_MOS_DERATE_END_TEMP_DC)
    {
        return min_u32(AURORA_THERMAL_MIN_POWER_MW, AURORA_RATED_POWER_MW);
    }

    {
        const uint32_t minimum_mw = min_u32(AURORA_THERMAL_MIN_POWER_MW,
                                             AURORA_RATED_POWER_MW);
        const uint32_t temperature_span_dC =
            (uint32_t)(AURORA_MOS_DERATE_END_TEMP_DC - AURORA_MOS_DERATE_TEMP_DC);
        const uint32_t elapsed_dC =
            (uint32_t)(sample->mos_temp_dC - AURORA_MOS_DERATE_TEMP_DC);
        const uint32_t reduction_mw =
            (uint32_t)(((uint64_t)(AURORA_RATED_POWER_MW - minimum_mw) * elapsed_dC) /
                       temperature_span_dC);
        return AURORA_RATED_POWER_MW - reduction_mw;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t battery_to_pv_power_mw(uint32_t battery_power_mw,
 *               uint16_t efficiency_q15)
 * Input       : battery_power_mw - 电池侧目标功率；efficiency_q15 - 估算效率
 * Output      : 达成电池目标所需的PV侧功率，mW
 * Description : Ppv≈Pbat/η；避免把Vbat×Ibat直接误当PV输入功率上限。
 *---------------------------------------------------------------------------*/
static uint32_t battery_to_pv_power_mw(uint32_t battery_power_mw,
                                       uint16_t efficiency_q15)
{
    uint64_t pv_power_mw;

    if ((battery_power_mw == 0U) || (efficiency_q15 == 0U))
    {
        return 0U;
    }

    pv_power_mw = ((uint64_t)battery_power_mw * DRV_DUTY_Q15_ONE +
                   efficiency_q15 - 1U) /
                  efficiency_q15;
    return (pv_power_mw > AURORA_RATED_POWER_MW) ?
               AURORA_RATED_POWER_MW : (uint32_t)pv_power_mw;
}

/*---------------------------------------------------------------------------*
 * Name        : static void atomic_or_u32(volatile uint32_t *target,
 *               uint32_t value)
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
 * Name        : static bool relay_close_still_safe(const aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : true表示当前这一刻仍允许物理吸合继电器
 * Description : 在APP完成1s压差确认后，真正写GPIO前再次检查最新BST_U/BAT_U、故障和PWM实际状态。
 *---------------------------------------------------------------------------*/
static bool relay_close_still_safe(const aurora_runtime_t *runtime)
{
    const aurora_measurement_t *sample = &runtime->app.sample;
    const uint32_t required = AURORA_MEAS_VALID_BAT_V | AURORA_MEAS_VALID_BUS_V;
    int64_t delta_mv;

    if ((runtime->app.power_command.state != AURORA_POWER_RELAY_SETTLE) ||
        runtime->app.power_command.pwm_enable ||
        (runtime->pending_fault_mask != 0U) ||
        !aurora_protection_is_safe(&runtime->app.protection) ||
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
 * Name        : static void force_safe_off(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : 统一物理关PWM、暂存0 Duty并撤销普通发波授权；不自动清Break锁存。
 *---------------------------------------------------------------------------*/
static void force_safe_off(aurora_runtime_t *runtime)
{
    drv_pwm_disarm();
    (void)drv_pwm_stage_duty(0U, NULL);
    runtime->pwm_arm_state = RUNTIME_PWM_ARM_OFF;
}

/*---------------------------------------------------------------------------*
 * Name        : static bool safety_still_clear(const aurora_runtime_t *runtime,
 *               uint32_t token)
 * Input       : runtime - 应用运行上下文；token - 安全epoch快照
 * Output      : true表示软件/硬件/人工门禁均仍安全
 * Description : 在真正放行PWM前后复核epoch、故障、Break和板级总门，阻断故障ISR后旧代码重新发波。
 *---------------------------------------------------------------------------*/
static bool safety_still_clear(const aurora_runtime_t *runtime, uint32_t token)
{
    return (runtime->safety_epoch == token) &&
           (runtime->pending_fault_mask == 0U) &&
           aurora_protection_is_safe(&runtime->app.protection) &&
           !drv_pwm_break_source_active() &&
           !drv_pwm_break_latched() &&
           drv_board_power_gate_open();
}

/*---------------------------------------------------------------------------*
 * Name        : static void clear_startup_break_if_safe(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : PWM从未输出时CMP只保留诊断；实时源已消失且无快速OCP锁存时显式清理遗留Break。
 *---------------------------------------------------------------------------*/
static void clear_startup_break_if_safe(aurora_runtime_t *runtime)
{
    const uint32_t faults = aurora_protection_fault_mask(&runtime->app.protection);

    if (!drv_pwm_output_active() && drv_pwm_break_latched() &&
        !drv_pwm_break_source_active() &&
        ((faults & RUNTIME_FAST_OCP_MASK) == 0U))
    {
        (void)drv_pwm_clear_break_latch();
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void runtime_fast_ocp_recovery(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 运行阶段CMP OCP需硬件源连续消失30s，PWM保持关闭后才清Break和软件锁存。
 *---------------------------------------------------------------------------*/
static void runtime_fast_ocp_recovery(aurora_runtime_t *runtime, uint32_t now_ms)
{
    const uint32_t fault_mask = aurora_protection_fault_mask(&runtime->app.protection);

    if ((fault_mask & RUNTIME_FAST_OCP_MASK) == 0U)
    {
        runtime->fast_ocp_recover_since_ms = 0U;
        return;
    }
    if (drv_pwm_break_source_active())
    {
        runtime->fast_ocp_recover_since_ms = 0U;
        return;
    }
    if (runtime->fast_ocp_recover_since_ms == 0U)
    {
        runtime->fast_ocp_recover_since_ms = now_ms;
        return;
    }
    if ((now_ms - runtime->fast_ocp_recover_since_ms) < AURORA_FAST_OCP_RECOVER_DELAY_MS)
    {
        return;
    }

    force_safe_off(runtime);
    if (drv_pwm_clear_break_latch() &&
        aurora_protection_clear_verified_fast_fault(&runtime->app.protection,
                                                     (uint32_t)RUNTIME_FAST_OCP_MASK,
                                                     true))
    {
        runtime->safety_epoch++;
        runtime->fast_ocp_recover_since_ms = 0U;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void apply_power_command(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : 严格按“先关PWM再切继电器；首次PWM先0CCR自然UEV；最终多次安全复核”落实应用命令。
 *---------------------------------------------------------------------------*/
static void apply_power_command(aurora_runtime_t *runtime)
{
    const aurora_power_command_t *command = &runtime->app.power_command;
    uint32_t token;
    aurora_irq_state_t irq;

    if (command->relay_enable != runtime->relay_applied)
    {
        /* 任何继电器切换先物理关PWM；闭合时必须实时复核BST_U/BAT_U压差。 */
        force_safe_off(runtime);

        if (command->relay_enable && !relay_close_still_safe(runtime))
        {
            drv_io_set_relay(false);
            runtime->relay_applied = false;
            return;
        }

        drv_io_set_relay(command->relay_enable);
        runtime->relay_applied = command->relay_enable;
        return;
    }

    if (!command->pwm_enable || !aurora_protection_is_safe(&runtime->app.protection))
    {
        force_safe_off(runtime);
        return;
    }

    clear_startup_break_if_safe(runtime);

    if (runtime->pwm_arm_state == RUNTIME_PWM_ARM_OFF)
    {
        drv_pwm_disarm();
        if (drv_pwm_prepare_arm_zero(&runtime->pwm_zero_sequence))
        {
            runtime->pwm_arm_state = RUNTIME_PWM_ARM_WAIT_ZERO;
        }
        return;
    }

    if (runtime->pwm_arm_state == RUNTIME_PWM_ARM_WAIT_ZERO)
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

        if (!safety_still_clear(runtime, token) || !drv_pwm_output_active())
        {
            force_safe_off(runtime);
            return;
        }
        runtime->pwm_arm_state = RUNTIME_PWM_ARM_ACTIVE;
        return;
    }

    token = runtime->safety_epoch;
    if (!safety_still_clear(runtime, token))
    {
        force_safe_off(runtime);
        return;
    }

    /* 只写CCR shadow；运行期禁止软件UG，Duty必须在下一自然UPDATE边界生效。 */
    if (!drv_pwm_stage_duty(command->duty_q15, NULL) ||
        !safety_still_clear(runtime, token))
    {
        force_safe_off(runtime);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_adc(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : 原子领取DMA完成块并标记处理中；同半块被DMA追上时ISR锁存overrun。
 *---------------------------------------------------------------------------*/
static void process_adc(aurora_runtime_t *runtime)
{
    uint8_t mask;
    uint8_t index;
    aurora_irq_state_t irq;

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
                runtime->watchdog_seen |= RUNTIME_WDG_TICKET_ADC;
            }

            irq = drv_irq_save();
            runtime->adc_processing_mask &= (uint8_t)~bit;
            drv_irq_restore(irq);
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void process_uart(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按预算消费RX环形缓冲、推进协议解析并发送应答，避免通信长期占用主循环。
 *---------------------------------------------------------------------------*/
static void process_uart(aurora_runtime_t *runtime, uint32_t now_ms)
{
    aurora_protocol_frame_t request;
    aurora_protocol_frame_t response;
    bool has_response;
    uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
    size_t wire_length;
    uint32_t budget = AURORA_RUNTIME_UART_RX_BUDGET;

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
        runtime->uart_tail =
            (uint16_t)((runtime->uart_tail + 1U) % sizeof(runtime->uart_rx));
        drv_irq_restore(irq);
        budget--;

        aurora_protocol_feed_byte(&runtime->app.protocol, byte, now_ms);
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
        }
    }

    if (runtime->uart_tail != runtime->uart_head)
    {
        atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_UART_RX);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void load_storage(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : 读取Flash A/B页，按回绕安全序号选择最新有效记录并重新初始化充电启动链。
 *---------------------------------------------------------------------------*/
static void load_storage(aurora_runtime_t *runtime)
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
        aurora_app_apply_settings(&runtime->app,
                                  &runtime->app.storage.settings,
                                  drv_time_now_ms());
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static void runtime_storage(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 只有PWM关闭且物理继电器断开时执行双页Journal保存，Commit Marker最后写入。
 *---------------------------------------------------------------------------*/
static void runtime_storage(aurora_runtime_t *runtime, uint32_t now_ms)
{
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t target;
    size_t used;

    if (!runtime->app.storage.dirty ||
        ((now_ms - runtime->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        drv_pwm_output_active() || runtime->relay_applied)
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
                                           AURORA_FAULT_STORAGE, now_ms);
        return;
    }

    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET,
                           &((uint32_t){AURORA_STORAGE_COMMIT_MARKER}),
                           sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&runtime->app.protection,
                                           AURORA_FAULT_STORAGE, now_ms);
        return;
    }
    runtime->app.storage.dirty = false;
}

/*---------------------------------------------------------------------------*
 * Name        : static void runtime_watchdog(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 核对主循环、控制和需要ADC的功率状态；只有健康票据齐全才刷新IWDT。
 *---------------------------------------------------------------------------*/
static void runtime_watchdog(aurora_runtime_t *runtime, uint32_t now_ms)
{
    uint32_t required = RUNTIME_WDG_TICKET_MAIN | RUNTIME_WDG_TICKET_CONTROL;

    if ((runtime->app.power_stage.state == AURORA_POWER_ZERO_CAL) ||
        (runtime->app.power_stage.state == AURORA_POWER_PRECHARGE) ||
        (runtime->app.power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (runtime->app.power_stage.state == AURORA_POWER_BAT_STABILITY) ||
        (runtime->app.power_stage.state == AURORA_POWER_RUN))
    {
        required |= RUNTIME_WDG_TICKET_ADC;
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
            drv_watchdog_feed();
        }
        runtime->watchdog_seen = 0U;
        runtime->watchdog_window_start_ms = now_ms;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_init(aurora_app_t *app,
 *               const aurora_measurement_calibration_t *calibration,
 *               uint32_t now_ms)
 * Input       : app - 应用总上下文；calibration - 六通道标定；now_ms - 当前毫秒
 * Output      : 无
 * Description : 按依赖顺序初始化测量、MPPT、保护、功率级、UI、协议、存储和充电器。
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
 * Input       : app - 应用总上下文；settings - 已校验设置；now_ms - 当前毫秒
 * Output      : 无
 * Description : 切换电池档案时撤销旧PI/Duty/启动状态，并要求重新零点校准、预充和电池稳定验证。
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
    aurora_charger_init(&app->charger, settings->chemistry, settings->pack, now_ms);
    aurora_mppt_reset(&app->mppt);
    aurora_power_stage_init(&app->power_stage, now_ms);
    aurora_measurement_zero_cal_reset(&app->measurement);
    memset(&app->charge_output, 0, sizeof(app->charge_output));
    memset(&app->mppt_output, 0, sizeof(app->mppt_output));
    memset(&app->power_command, 0, sizeof(app->power_command));
    app->link_request = false;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_adc_block(aurora_app_t *app,
 *               const uint16_t *raw, size_t word_count, uint32_t timestamp_ms)
 * Input       : app - 应用总上下文；raw - 完整DMA块；word_count - 块字数；timestamp_ms - 完成时间
 * Output      : 无
 * Description : 处理完整DMA块；ZERO_CAL阶段且PV已稳定2s时额外累计PV_I运行时零点。
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

    if ((app->power_stage.state == AURORA_POWER_ZERO_CAL) &&
        !app->power_stage.relay_closed &&
        (app->power_stage.pv_valid_since_ms != 0U) &&
        ((timestamp_ms - app->power_stage.pv_valid_since_ms) >=
         AURORA_ZERO_CAL_PV_STABLE_MS))
    {
        (void)aurora_measurement_zero_cal_accumulate(&app->measurement,
                                                     raw,
                                                     word_count);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_fast_fault(aurora_app_t *app,
 *               uint32_t fault_mask, uint32_t now_ms)
 * Input       : app - 应用总上下文；fault_mask - ISR快速故障；now_ms - 当前毫秒
 * Output      : 无
 * Description : 把真正运行阶段的快速硬件故障纳入统一锁存；恢复由运行层和保护策略共同裁决。
 *---------------------------------------------------------------------------*/
void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms)
{
    if (app != NULL)
    {
        aurora_protection_latch_fast_fault(&app->protection, fault_mask, now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms,
 *               bool boost_output_active)
 * Input       : app - 应用总上下文；now_ms - 当前毫秒；boost_output_active - Driver报告的PWM实际状态
 * Output      : 无
 * Description : 1ms执行保护和Power Stage；10ms更新Charger/MPPT/UI，并形成最终功率命令。
 *---------------------------------------------------------------------------*/
void aurora_app_step_1ms(aurora_app_t *app,
                         uint32_t now_ms,
                         bool boost_output_active)
{
    uint32_t elapsed_step_ms;
    aurora_power_state_t previous_power_state;

    if (app == NULL)
    {
        return;
    }

    elapsed_step_ms = now_ms - app->last_step_ms;
    if (elapsed_step_ms == 0U)
    {
        return;
    }
    if (elapsed_step_ms > AURORA_MAX_ELAPSED_STEP_MS)
    {
        elapsed_step_ms = AURORA_MAX_ELAPSED_STEP_MS;
    }
    app->last_step_ms = now_ms;

    (void)aurora_measurement_read(&app->measurement, &app->sample);

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

    aurora_measurement_estimate_battery_current(
        &app->sample,
        estimate_efficiency_q15(&app->sample),
        app->power_stage.relay_closed,
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (app->power_stage.state == AURORA_POWER_BAT_STABILITY));

    aurora_protection_step(&app->protection,
                           &app->sample,
                           &app->charger.profile,
                           boost_output_active,
                           now_ms);

    if ((now_ms - app->last_10ms) >= AURORA_CONTROL_PERIOD_MS)
    {
        const uint32_t elapsed_control_ms = now_ms - app->last_10ms;
        const bool weak_light = app->sample.pv_power_mw < APP_WEAK_LIGHT_POWER_MW;
        const uint32_t thermal_limit_mw = thermal_power_limit_mw(&app->sample);
        const uint32_t voltage_limit_mw =
            input_voltage_power_limit_mw(app->sample.pv_voltage_mv);
        uint32_t current_limit_mw = AURORA_RATED_POWER_MW;
        uint32_t hardware_limit_mw;
        uint32_t pv_required_mw;
        uint16_t efficiency_q15;
        bool input_limited_previous = app->charge_output.input_limited;
        bool thermal_limited = thermal_limit_mw < AURORA_RATED_POWER_MW;
        bool external_limited;

        if (app->sample.pv_voltage_mv > 0)
        {
            const uint64_t current_power =
                ((uint64_t)(uint32_t)app->sample.pv_voltage_mv *
                 (uint32_t)AURORA_PV_CURRENT_LIMIT_MA) /
                AURORA_MV_MA_PER_MW;
            if (current_power < current_limit_mw)
            {
                current_limit_mw = (uint32_t)current_power;
            }
        }

        hardware_limit_mw = min_u32(AURORA_RATED_POWER_MW, thermal_limit_mw);
        hardware_limit_mw = min_u32(hardware_limit_mw, voltage_limit_mw);
        hardware_limit_mw = min_u32(hardware_limit_mw, current_limit_mw);

        app->charge_output = aurora_charger_step(&app->charger,
                                                 &app->sample,
                                                 weak_light,
                                                 thermal_limited,
                                                 input_limited_previous,
                                                 now_ms);

        efficiency_q15 = estimate_efficiency_q15(&app->sample);
        pv_required_mw = battery_to_pv_power_mw(
            app->charge_output.battery_power_target_mw, efficiency_q15);
        app->charge_output.pv_power_limit_mw =
            min_u32(pv_required_mw, hardware_limit_mw);
        app->charge_output.input_limited =
            app->charge_output.allow_charge && (pv_required_mw > hardware_limit_mw);
        app->charge_output.thermal_limited = thermal_limited;

        external_limited = thermal_limited ||
                           app->charge_output.input_limited ||
                           !app->charge_output.allow_charge;
        app->mppt_output = aurora_mppt_step(&app->mppt,
                                            &app->sample,
                                            app->charge_output.pv_power_limit_mw,
                                            external_limited,
                                            now_ms);
        app->ui_output = aurora_ui_step(&app->ui,
                                        app->power_stage.state,
                                        aurora_protection_fault_mask(&app->protection),
                                        elapsed_control_ms);
        app->last_10ms = now_ms;
    }

    previous_power_state = app->power_stage.state;
    app->power_command = aurora_power_stage_step(
        &app->power_stage,
        &app->sample,
        &app->mppt_output,
        &app->charge_output,
        aurora_protection_is_safe(&app->protection),
        aurora_measurement_zero_cal_ready(&app->measurement),
        aurora_measurement_zero_cal_failed(&app->measurement),
        now_ms);

    if ((app->power_stage.state == AURORA_POWER_START_DELAY) &&
        (previous_power_state != AURORA_POWER_START_DELAY))
    {
        aurora_measurement_zero_cal_reset(&app->measurement);
    }

    app->link_request =
        (app->sample.pv_voltage_mv >= AURORA_PV_START_MIN_MV) ||
        (app->power_stage.state == AURORA_POWER_PRECHARGE) ||
        (app->power_stage.state == AURORA_POWER_RELAY_SETTLE) ||
        (app->power_stage.state == AURORA_POWER_BAT_STABILITY) ||
        (app->power_stage.state == AURORA_POWER_RUN);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_app_on_protocol_frame(aurora_app_t *app,
 *               const aurora_protocol_frame_t *frame,
 *               aurora_protocol_frame_t *response, bool *has_response,
 *               uint32_t now_ms)
 * Input       : app/frame/response/has_response - 协议上下文；now_ms - 当前毫秒
 * Output      : 无；通过response/has_response返回可选应答
 * Description : 处理电池档案设置和能量复位；运行期改档案立即撤销旧控制并重新完整启动。
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

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_runtime_init(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : true表示关键驱动、业务模块和ADC采样链全部初始化成功
 * Description : MCU供电资格已由main()先行确认；本函数建立IRQ/IWDT/PWM/COMP/ADC/UART、业务状态和Flash参数。
 *---------------------------------------------------------------------------*/
bool aurora_runtime_init(aurora_runtime_t *runtime)
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

    drv_irq_configure_priorities();
    drv_io_set_relay(false);
    drv_io_set_link(false);
    drv_io_set_leds(false, false);
    runtime->relay_applied = false;

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
        }
    }

    aurora_app_init(&runtime->app, &calibration, now_ms);
    load_storage(runtime);
    aurora_debug_init();
    runtime->safety_epoch = 1U;
    runtime->last_telemetry_ms = now_ms;

    if (!drv_adc_start())
    {
        return false;
    }
    runtime->initialized = true;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_runtime_poll(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : 主循环消费故障、ADC、UART和Tick事件，再落实功率命令、遥测、存储和看门狗健康监督。
 *---------------------------------------------------------------------------*/
void aurora_runtime_poll(aurora_runtime_t *runtime)
{
    uint32_t events;
    uint32_t now_ms;

    if ((runtime == NULL) || !runtime->initialized)
    {
        return;
    }

    now_ms = drv_time_now_ms();
    events = atomic_exchange_u32(&runtime->event_flags, 0U);
    runtime->watchdog_seen |= RUNTIME_WDG_TICKET_MAIN;

    if ((events & RUNTIME_EVENT_FAST_FAULT) != 0U)
    {
        const uint32_t faults = atomic_exchange_u32(&runtime->pending_fault_mask, 0U);
        aurora_app_on_fast_fault(&runtime->app, faults, now_ms);
        force_safe_off(runtime);
    }
    if ((events & RUNTIME_EVENT_ADC) != 0U)
    {
        process_adc(runtime);
    }
    if ((events & RUNTIME_EVENT_UART_RX) != 0U)
    {
        process_uart(runtime, now_ms);
    }
    if ((events & RUNTIME_EVENT_TICK) != 0U)
    {
        aurora_app_step_1ms(&runtime->app, now_ms, drv_pwm_output_active());
        drv_io_set_leds(runtime->app.ui_output.led_run_on,
                        runtime->app.ui_output.led_fault_on);
        drv_io_set_link(runtime->app.link_request);
        runtime->watchdog_seen |= RUNTIME_WDG_TICKET_CONTROL;
    }

    runtime_fast_ocp_recovery(runtime, now_ms);
    apply_power_command(runtime);

    if ((now_ms - runtime->last_telemetry_ms) >= AURORA_TELEMETRY_PERIOD_MS)
    {
        aurora_protocol_frame_t telemetry;
        uint8_t wire[AURORA_PROTOCOL_MAX_WIRE];
        size_t wire_length;

        aurora_protocol_fill_telemetry(&telemetry,
                                       runtime->app.telemetry_message_id++,
                                       &runtime->app.sample,
                                       runtime->app.charger.state,
                                       aurora_protection_fault_mask(&runtime->app.protection),
                                       &runtime->app.storage.settings);
        wire_length = aurora_protocol_encode(&telemetry, wire, sizeof(wire));
        if (wire_length != 0U)
        {
            (void)drv_uart_send(wire, wire_length);
        }
        runtime->last_telemetry_ms = now_ms;
    }

    runtime_storage(runtime, now_ms);
    runtime_watchdog(runtime, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_runtime_isr_tick(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : SysTick只递增时间并投递1ms事件，不在ISR执行控制算法。
 *---------------------------------------------------------------------------*/
void aurora_runtime_isr_tick(aurora_runtime_t *runtime)
{
    if (runtime != NULL)
    {
        drv_time_tick_isr();
        atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_TICK);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_runtime_isr_adc_block(aurora_runtime_t *runtime,
 *               uint8_t block_index)
 * Input       : runtime - 应用运行上下文；block_index - DMA半缓冲索引
 * Output      : 无
 * Description : 发布完整DMA半块；同一半块覆盖正在处理的数据时锁存ADC_OVERRUN。
 *---------------------------------------------------------------------------*/
void aurora_runtime_isr_adc_block(aurora_runtime_t *runtime, uint8_t block_index)
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
            aurora_runtime_isr_fast_fault(runtime, AURORA_FAULT_ADC_OVERRUN);
        }
        else
        {
            atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_ADC);
        }
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_runtime_isr_fast_fault(aurora_runtime_t *runtime,
 *               uint32_t fault_mask)
 * Input       : runtime - 应用运行上下文；fault_mask - 快速故障位
 * Output      : 无
 * Description : 通用快速故障第一动作物理关PWM，随后递增epoch并投递主循环锁存。
 *---------------------------------------------------------------------------*/
void aurora_runtime_isr_fast_fault(aurora_runtime_t *runtime, uint32_t fault_mask)
{
    if (runtime != NULL)
    {
        drv_pwm_force_off_isr();
        runtime->safety_epoch++;
        atomic_or_u32(&runtime->pending_fault_mask, fault_mask);
        atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_FAST_FAULT);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_runtime_isr_comparator_fault(
 *               aurora_runtime_t *runtime, uint32_t fault_mask)
 * Input       : runtime - 应用运行上下文；fault_mask - COMP映射后的OCP故障位
 * Output      : 无
 * Description : 硬件始终可关波；PWM未实际输出时只记录诊断，运行中则进入快速故障锁存链。
 *---------------------------------------------------------------------------*/
void aurora_runtime_isr_comparator_fault(aurora_runtime_t *runtime, uint32_t fault_mask)
{
    bool pwm_was_active;

    if (runtime == NULL)
    {
        return;
    }

    pwm_was_active = drv_pwm_output_active();
    drv_pwm_force_off_isr();
    if (!pwm_was_active)
    {
        runtime->startup_comp_ignored_count++;
        return;
    }

    runtime->safety_epoch++;
    atomic_or_u32(&runtime->pending_fault_mask, fault_mask);
    atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_FAST_FAULT);
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_runtime_isr_uart_rx(aurora_runtime_t *runtime,
 *               uint8_t byte)
 * Input       : runtime - 应用运行上下文；byte - 接收字节
 * Output      : 无
 * Description : ISR只把字节写入固定环形缓冲，满时累计溢出，不解析完整协议帧。
 *---------------------------------------------------------------------------*/
void aurora_runtime_isr_uart_rx(aurora_runtime_t *runtime, uint8_t byte)
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
        atomic_or_u32(&runtime->event_flags, RUNTIME_EVENT_UART_RX);
    }
    else
    {
        runtime->uart_rx_overrun_count++;
    }
}

#if defined(G32F031xx)
/*---------------------------------------------------------------------------*
 * Name        : int main(void)
 * Input       : 无
 * Output      : 无（正常不返回）
 * Description : 两层架构系统入口：先建立系统时基和功率GPIO安全态，再等待MCU VDD稳定；
 *               通过后初始化应用运行层并持续调度业务。所有硬件动作均经Driver接口完成。
 *---------------------------------------------------------------------------*/
int main(void)
{
    drv_system_init();
    drv_io_init();
    drv_io_set_relay(false);
    drv_io_set_link(false);
    drv_io_set_leds(false, false);

    /* 弱光供电不稳定只会停在这里等待，不产生Fault，也不会提前启动IWDT/PWM/ADC。 */
    if (!drv_system_wait_for_supply_stable())
    {
        for (;;)
        {
            /* PVD/时钟模块自身建立异常：保持最小安全态，等待调试或外部硬件复位。 */
        }
    }

    if (!aurora_runtime_init(&g_aurora_runtime))
    {
        for (;;)
        {
            /* 完整初始化失败后不继续控制；若IWDT已启动，将由硬件复位。 */
        }
    }

    for (;;)
    {
        aurora_runtime_poll(&g_aurora_runtime);
    }
}
#endif
