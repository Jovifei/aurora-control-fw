#ifndef AURORA_PROTECTION_H
#define AURORA_PROTECTION_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t active_mask;
    uint32_t latched_mask;
    uint32_t epoch;
    uint32_t first_fault_ms;
    uint16_t pv_uv_count;
    uint16_t pv_ov_count;
    uint16_t bat_uv_count;
    uint16_t bat_ov_count;
    uint16_t mos_temp_count;
    uint16_t amb_temp_count;
    uint32_t startup_ms;
    bool measurement_seen;
} aurora_protection_ctx_t;

void aurora_protection_init(aurora_protection_ctx_t *ctx, uint32_t now_ms);
void aurora_protection_latch_fast_fault(aurora_protection_ctx_t *ctx,
                                        uint32_t fault_mask,
                                        uint32_t now_ms);
void aurora_protection_step(aurora_protection_ctx_t *ctx,
                            const aurora_measurement_t *sample,
                            const aurora_charge_profile_t *profile,
                            uint32_t now_ms);
bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
                             uint32_t clear_mask,
                             bool hardware_sources_inactive);
bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx);
uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
