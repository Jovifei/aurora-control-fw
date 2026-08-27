#ifndef AURORA_MPPT_H
#define AURORA_MPPT_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    AURORA_MPPT_DISABLED = 0,
    AURORA_MPPT_FAST_DESCENT,
    AURORA_MPPT_TRACKING,
    AURORA_MPPT_LIMITED
} aurora_mppt_state_t;

typedef struct
{
    aurora_mppt_state_t state;
    uint32_t target_voltage_mv;
    uint32_t open_circuit_voltage_mv;
    int32_t previous_voltage_mv;
    int32_t previous_power_mw;
    int64_t integral_mw;
    uint32_t last_search_ms;
    uint32_t last_pi_ms;
    bool previous_valid;
} aurora_mppt_ctx_t;

void aurora_mppt_init(aurora_mppt_ctx_t *ctx);
void aurora_mppt_reset(aurora_mppt_ctx_t *ctx);
void aurora_mppt_set_open_circuit_voltage(aurora_mppt_ctx_t *ctx, uint32_t voc_mv);
aurora_mppt_output_t aurora_mppt_step(aurora_mppt_ctx_t *ctx,
                                      const aurora_measurement_t *sample,
                                      uint32_t power_allow_mw,
                                      bool external_limited,
                                      uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
