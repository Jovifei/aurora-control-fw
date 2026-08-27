#ifndef AURORA_UI_H
#define AURORA_UI_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LED闪烁相位上下文。 */
typedef struct
{
    uint32_t led_phase_ms;                           /* 当前相位内累计时间，ms。 */
} aurora_ui_ctx_t;

void aurora_ui_init(aurora_ui_ctx_t *ctx);
aurora_ui_output_t aurora_ui_step(aurora_ui_ctx_t *ctx,
                                  aurora_power_state_t power_state,
                                  uint32_t fault_mask,
                                  uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif
