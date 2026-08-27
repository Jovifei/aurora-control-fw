#ifndef AURORA_POWER_STAGE_H
#define AURORA_POWER_STAGE_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    aurora_power_state_t state;
    uint32_t state_since_ms;
    uint32_t delta_ok_since_ms;
    uint32_t no_sun_since_ms;
    uint16_t duty_q15;
    int64_t power_integral;
    bool relay_closed;
} aurora_power_stage_ctx_t;

void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx, uint32_t now_ms);
aurora_power_command_t aurora_power_stage_step(aurora_power_stage_ctx_t *ctx,
                                               const aurora_measurement_t *sample,
                                               const aurora_mppt_output_t *mppt,
                                               const aurora_charge_output_t *charger,
                                               bool protection_safe,
                                               uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
