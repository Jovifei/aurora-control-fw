#ifndef AURORA_CHARGER_H
#define AURORA_CHARGER_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    aurora_charge_state_t state;
    aurora_charge_profile_t profile;
    uint32_t state_since_ms;
    uint32_t charge_start_ms;
    uint32_t tail_since_ms;
    uint32_t float_start_ms;
    int64_t cv_integral_mw;
    bool initialized;
} aurora_charger_ctx_t;

bool aurora_charge_profile_get(aurora_battery_chem_t chemistry,
                               aurora_battery_pack_t pack,
                               aurora_charge_profile_t *out);
void aurora_charger_init(aurora_charger_ctx_t *ctx,
                         aurora_battery_chem_t chemistry,
                         aurora_battery_pack_t pack,
                         uint32_t now_ms);
aurora_charge_output_t aurora_charger_step(aurora_charger_ctx_t *ctx,
                                           const aurora_measurement_t *sample,
                                           bool weak_light,
                                           bool thermal_limited,
                                           uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
