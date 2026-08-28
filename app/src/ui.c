#include "ui.h"

#include "app_config.h"

#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : static uint8_t fault_blink_count(uint32_t fault_mask)
 * Input       : fault_mask - 当前active/latched故障并集
 * Output      : 1=PV类，2=电池类，3=硬件/温度/内部类，0=无故障
 * Description : 按V2.7优先级“硬件>电池>PV”把内部多位故障压缩为两灯可表达的组闪次数。
 *---------------------------------------------------------------------------*/
static uint8_t fault_blink_count(uint32_t fault_mask)
{
    const uint32_t hardware_mask =
        AURORA_FAULT_FAST_MOS_OCP |
        AURORA_FAULT_FAST_BREAK |
        AURORA_FAULT_BUS_OVERVOLT |
        AURORA_FAULT_MOS_OVERTEMP |
        AURORA_FAULT_AMB_TEMP |
        AURORA_FAULT_MOS_NTC_OPEN |
        AURORA_FAULT_MOS_NTC_SHORT |
        AURORA_FAULT_AMB_NTC_OPEN |
        AURORA_FAULT_AMB_NTC_SHORT |
        AURORA_FAULT_ADC_STALE |
        AURORA_FAULT_ADC_DMA |
        AURORA_FAULT_ADC_OVERRUN |
        AURORA_FAULT_RELAY |
        AURORA_FAULT_STORAGE |
        AURORA_FAULT_INTERNAL;
    const uint32_t battery_mask =
        AURORA_FAULT_BAT_UNDERVOLT |
        AURORA_FAULT_BAT_OVERVOLT |
        AURORA_FAULT_SETTINGS;
    const uint32_t pv_mask =
        AURORA_FAULT_FAST_PV_OCP |
        AURORA_FAULT_PV_UNDERVOLT |
        AURORA_FAULT_PV_OVERVOLT |
        AURORA_FAULT_PV_OVERCURRENT |
        AURORA_FAULT_PV_OVERPOWER;

    if ((fault_mask & hardware_mask) != 0U)
    {
        return 3U;
    }
    if ((fault_mask & battery_mask) != 0U)
    {
        return 2U;
    }
    if ((fault_mask & pv_mask) != 0U)
    {
        return 1U;
    }
    return 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_ui_init(aurora_ui_ctx_t *ctx)
 * Input       : ctx - UI上下文
 * Output      : 无
 * Description : 清零LED相位，使上电后的RUN/FAULT显示从确定相位开始。
 *---------------------------------------------------------------------------*/
void aurora_ui_init(aurora_ui_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        memset(ctx, 0, sizeof(*ctx));
    }
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_ui_output_t aurora_ui_step(aurora_ui_ctx_t *ctx,
 *               aurora_power_state_t power_state, uint32_t fault_mask,
 *               uint32_t elapsed_ms)
 * Input       : ctx - UI上下文；power_state - 功率级状态；fault_mask - 故障并集；elapsed_ms - 推进时间
 * Output      : RUN/FAULT两路逻辑点亮命令
 * Description : RUN在正常充电时闪烁、系统工作非充电时常亮；FAULT按1/2/3次组闪显示PV/电池/硬件类。
 *---------------------------------------------------------------------------*/
aurora_ui_output_t aurora_ui_step(aurora_ui_ctx_t *ctx,
                                  aurora_power_state_t power_state,
                                  uint32_t fault_mask,
                                  uint32_t elapsed_ms)
{
    aurora_ui_output_t output = {false, false};
    const uint8_t blink_count = fault_blink_count(fault_mask);

    if (ctx == NULL)
    {
        return output;
    }

    ctx->led_phase_ms += elapsed_ms;

    if (blink_count != 0U)
    {
        const uint32_t pulse_window_ms = (uint32_t)blink_count *
                                         (2U * AURORA_UI_FAULT_HALF_MS);
        const uint32_t group_ms = pulse_window_ms + AURORA_UI_FAULT_GROUP_GAP_MS;
        const uint32_t phase_ms = ctx->led_phase_ms % group_ms;

        /* 故障显示优先：RUN熄灭，FAULT按500ms亮/500ms灭完成1/2/3次组闪。 */
        if (phase_ms < pulse_window_ms)
        {
            output.led_fault_on =
                ((phase_ms % (2U * AURORA_UI_FAULT_HALF_MS)) <
                 AURORA_UI_FAULT_HALF_MS);
        }
        return output;
    }

    if (power_state == AURORA_POWER_RUN)
    {
        output.led_run_on =
            ((ctx->led_phase_ms % (2U * AURORA_UI_RUN_BLINK_HALF_MS)) <
             AURORA_UI_RUN_BLINK_HALF_MS);
    }
    else if ((power_state != AURORA_POWER_OFF) &&
             (power_state != AURORA_POWER_NO_SUN))
    {
        /* 等待、启动、校准、预充和电池稳定阶段表示MCU正在工作，RUN常亮。 */
        output.led_run_on = true;
    }

    return output;
}
