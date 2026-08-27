#include "protection.h"

#include "app_config.h"

#include <string.h>

#define SOFTWARE_DEBOUNCE_COUNT       (10U)
#define MEASUREMENT_STARTUP_GRACE_MS  (250U)

static void latch_fault(aurora_protection_ctx_t *ctx, uint32_t mask, uint32_t now_ms)
{
    if ((ctx->latched_mask == 0U) && (mask != 0U))
    {
        ctx->first_fault_ms = now_ms;
    }
    if ((ctx->latched_mask & mask) != mask)
    {
        ctx->epoch++;
    }
    ctx->active_mask |= mask;
    ctx->latched_mask |= mask;
}

static void debounce_fault(aurora_protection_ctx_t *ctx,
                           bool condition,
                           uint16_t *counter,
                           uint32_t mask,
                           uint32_t now_ms)
{
    if (condition)
    {
        if (*counter < SOFTWARE_DEBOUNCE_COUNT)
        {
            (*counter)++;
        }
        if (*counter >= SOFTWARE_DEBOUNCE_COUNT)
        {
            latch_fault(ctx, mask, now_ms);
        }
    }
    else
    {
        *counter = 0U;
        ctx->active_mask &= ~mask;
    }
}

void aurora_protection_init(aurora_protection_ctx_t *ctx, uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->epoch = 1U;
    ctx->startup_ms = now_ms;
}

void aurora_protection_latch_fast_fault(aurora_protection_ctx_t *ctx,
                                        uint32_t fault_mask,
                                        uint32_t now_ms)
{
    if (ctx != NULL)
    {
        latch_fault(ctx, fault_mask, now_ms);
    }
}

void aurora_protection_step(aurora_protection_ctx_t *ctx,
                            const aurora_measurement_t *sample,
                            const aurora_charge_profile_t *profile,
                            uint32_t now_ms)
{
    const uint32_t required = AURORA_MEAS_VALID_PV_V |
                              AURORA_MEAS_VALID_PV_I |
                              AURORA_MEAS_VALID_BAT_V |
                              AURORA_MEAS_VALID_BUS_V;

    if ((ctx == NULL) || (sample == NULL) || (profile == NULL))
    {
        return;
    }

    if ((sample->sequence != 0U) && ((sample->valid_mask & required) == required))
    {
        ctx->measurement_seen = true;
    }

    if (!ctx->measurement_seen)
    {
        if ((now_ms - ctx->startup_ms) > MEASUREMENT_STARTUP_GRACE_MS)
        {
            latch_fault(ctx, AURORA_FAULT_ADC_STALE, now_ms);
        }
        return;
    }

    if (((sample->valid_mask & required) != required) ||
        ((now_ms - sample->timestamp_ms) > AURORA_MEASUREMENT_STALE_MS))
    {
        latch_fault(ctx, AURORA_FAULT_ADC_STALE, now_ms);
        return;
    }
    ctx->active_mask &= (uint32_t)~AURORA_FAULT_ADC_STALE;

    /* 无光/低PV属于待机状态，不是需要人工清除的故障；由功率状态机处理30分钟断继电器。 */
    ctx->pv_uv_count = 0U;
    ctx->active_mask &= (uint32_t)~AURORA_FAULT_PV_UNDERVOLT;

    debounce_fault(ctx,
                   sample->pv_voltage_mv > AURORA_PV_ABSOLUTE_MAX_MV,
                   &ctx->pv_ov_count,
                   AURORA_FAULT_PV_OVERVOLT,
                   now_ms);

    /* 低于5V视为未接电池；已接入但低于档案下限才作为电池欠压。 */
    debounce_fault(ctx,
                   (sample->battery_voltage_mv > 5000) &&
                       ((uint32_t)sample->battery_voltage_mv < profile->battery_uv_mv),
                   &ctx->bat_uv_count,
                   AURORA_FAULT_BAT_UNDERVOLT,
                   now_ms);
    debounce_fault(ctx,
                   (sample->battery_voltage_mv > 0) &&
                       ((uint32_t)sample->battery_voltage_mv > profile->cv_protect_mv),
                   &ctx->bat_ov_count,
                   AURORA_FAULT_BAT_OVERVOLT,
                   now_ms);

    {
        const bool mos_valid = (sample->valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U;
        const bool mos_active = (ctx->active_mask & AURORA_FAULT_MOS_OVERTEMP) != 0U;
        const bool mos_fault = mos_valid &&
                               (sample->mos_temp_dC >
                                (mos_active ? AURORA_MOS_RECOVER_TEMP_DC :
                                              AURORA_MOS_TRIP_TEMP_DC));
        debounce_fault(ctx,
                       mos_fault,
                       &ctx->mos_temp_count,
                       AURORA_FAULT_MOS_OVERTEMP,
                       now_ms);
    }

    {
        const bool amb_valid = (sample->valid_mask & AURORA_MEAS_VALID_AMB_TEMP) != 0U;
        const bool amb_active = (ctx->active_mask & AURORA_FAULT_AMB_TEMP) != 0U;
        const int16_t low_limit = amb_active ?
                                      (int16_t)(AURORA_AMB_MIN_TEMP_DC +
                                                AURORA_AMB_RECOVER_MARGIN_DC) :
                                      (int16_t)AURORA_AMB_MIN_TEMP_DC;
        const int16_t high_limit = amb_active ?
                                       (int16_t)(AURORA_AMB_MAX_TEMP_DC -
                                                 AURORA_AMB_RECOVER_MARGIN_DC) :
                                       (int16_t)AURORA_AMB_MAX_TEMP_DC;
        const bool amb_fault = amb_valid &&
                               ((sample->ambient_temp_dC < low_limit) ||
                                (sample->ambient_temp_dC > high_limit));
        debounce_fault(ctx,
                       amb_fault,
                       &ctx->amb_temp_count,
                       AURORA_FAULT_AMB_TEMP,
                       now_ms);
    }
}

bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
                             uint32_t clear_mask,
                             bool hardware_sources_inactive)
{
    if ((ctx == NULL) || !hardware_sources_inactive || ((ctx->active_mask & clear_mask) != 0U))
    {
        return false;
    }
    ctx->latched_mask &= ~clear_mask;
    ctx->epoch++;
    return true;
}

bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) && (ctx->active_mask == 0U) && (ctx->latched_mask == 0U);
}

uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) ? ctx->epoch : 0U;
}
