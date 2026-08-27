#include "ui.h"

#include "app_config.h"

#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : void aurora_ui_init(aurora_ui_ctx_t *ctx)
 * Input       : ctx - UI上下文
 * Output      : 无
 * Description : 清零LED相位计时器，使上电后的指示序列从确定相位开始。
 *---------------------------------------------------------------------------*/
void aurora_ui_init(aurora_ui_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        memset(ctx, 0, sizeof(*ctx));
    }
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_ui_output_t aurora_ui_step(aurora_ui_ctx_t *ctx, aurora_power_state_t power_state, uint32_t fault_mask, uint32_t elapsed_ms)
 * Input       : ctx - UI上下文；power_state - 功率级状态；fault_mask - 锁存故障位；elapsed_ms - 本次推进时间
 * Output      : RUN/FAULT两路逻辑点亮命令
 * Description : 按故障优先、运行常亮、待机短闪的规则生成LED逻辑；物理高低有效由驱动层转换。
 *---------------------------------------------------------------------------*/
aurora_ui_output_t aurora_ui_step(aurora_ui_ctx_t *ctx,
                                  aurora_power_state_t power_state,
                                  uint32_t fault_mask,
                                  uint32_t elapsed_ms)
{
    aurora_ui_output_t output = {false, false};

    if (ctx == NULL)
    {
        return output;
    }

    ctx->led_phase_ms = (ctx->led_phase_ms + elapsed_ms) %
                        AURORA_UI_PHASE_PERIOD_MS;

    if (fault_mask != 0U)
    {
        /* 故障具有最高显示优先级：RUN熄灭，FAULT快速闪烁。 */
        output.led_fault_on =
            ((ctx->led_phase_ms % AURORA_UI_FAULT_BLINK_PERIOD_MS) <
             AURORA_UI_FAULT_ON_TIME_MS);
    }
    else if ((power_state == AURORA_POWER_RUN) ||
             (power_state == AURORA_POWER_PRECHARGE) ||
             (power_state == AURORA_POWER_RELAY_SETTLE))
    {
        /* 正在建立或传输功率时RUN常亮。 */
        output.led_run_on = true;
    }
    else
    {
        /* 无故障待机用慢速短闪表示MCU仍在运行。 */
        output.led_run_on =
            (ctx->led_phase_ms < AURORA_UI_STANDBY_PULSE_MS);
    }

    return output;
}
