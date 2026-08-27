#include "ui.h"

#include <string.h>

void aurora_ui_init(aurora_ui_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        memset(ctx, 0, sizeof(*ctx));
    }
}

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

    ctx->led_phase_ms = (ctx->led_phase_ms + elapsed_ms) % 2000U;
    if (fault_mask != 0U)
    {
        output.led_run_on = false;
        output.led_fault_on = (ctx->led_phase_ms % 200U) < 100U;
    }
    else if (power_state == AURORA_POWER_RUN)
    {
        output.led_run_on = true;
        output.led_fault_on = false;
    }
    else
    {
        output.led_run_on = ctx->led_phase_ms < 200U;
        output.led_fault_on = false;
    }
    return output;
}
