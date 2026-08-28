#include "protection.h"

#include "app_config.h"

#include <string.h>

/*---------------------------------------------------------------------------*
 * Name        : static void latch_fault(aurora_protection_ctx_t *ctx,
 *               uint32_t mask, uint32_t now_ms)
 * Input       : ctx - 保护上下文；mask - 需要锁存的故障位；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 同时更新active/latched掩码；首次故障记录时间，新增故障位时递增安全epoch。
 *---------------------------------------------------------------------------*/
static void latch_fault(aurora_protection_ctx_t *ctx,
                        uint32_t mask,
                        uint32_t now_ms)
{
    if ((ctx->latched_mask == 0U) && (mask != 0U))
    {
        ctx->first_fault_ms = now_ms;
    }

    if ((ctx->latched_mask & mask) != mask)
    {
        /* epoch变化会使Service中已经取得的旧发波许可立即失效。 */
        ctx->epoch++;
    }

    ctx->active_mask |= mask;
    ctx->latched_mask |= mask;
}

/*---------------------------------------------------------------------------*
 * Name        : static void debounce_fault(aurora_protection_ctx_t *ctx,
 *               bool condition, uint16_t *counter, uint32_t mask,
 *               uint32_t now_ms)
 * Input       : ctx - 保护上下文；condition - 当前故障条件；counter - 独立去抖计数器；
 *               mask - 对应故障位；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 条件连续成立到阈值后锁存；条件恢复只清active和计数，不自动清除latched。
 *---------------------------------------------------------------------------*/
static void debounce_fault(aurora_protection_ctx_t *ctx,
                           bool condition,
                           uint16_t *counter,
                           uint32_t mask,
                           uint32_t now_ms)
{
    if (condition)
    {
        if (*counter < AURORA_PROTECTION_DEBOUNCE_SAMPLES)
        {
            (*counter)++;
        }
        if (*counter >= AURORA_PROTECTION_DEBOUNCE_SAMPLES)
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

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_init(aurora_protection_ctx_t *ctx,
 *               uint32_t now_ms)
 * Input       : ctx - 保护上下文；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 清零全部故障、计数和时间状态，并建立初始epoch及首测量宽限起点。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_latch_fast_fault(
 *               aurora_protection_ctx_t *ctx, uint32_t fault_mask,
 *               uint32_t now_ms)
 * Input       : ctx - 保护上下文；fault_mask - ISR上报的快速故障位；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 把比较器、Break、ADC DMA和overrun等快速路径故障纳入统一软件锁存。
 *---------------------------------------------------------------------------*/
void aurora_protection_latch_fast_fault(aurora_protection_ctx_t *ctx,
                                        uint32_t fault_mask,
                                        uint32_t now_ms)
{
    if (ctx != NULL)
    {
        latch_fault(ctx, fault_mask, now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_protection_step(aurora_protection_ctx_t *ctx,
 *               const aurora_measurement_t *sample,
 *               const aurora_charge_profile_t *profile, uint32_t now_ms)
 * Input       : ctx - 保护上下文；sample - 最新测量快照；profile - 当前电池档案；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 检查测量完整性/时效、电压和温度边界，并按独立去抖计数更新软件保护。
 *---------------------------------------------------------------------------*/
void aurora_protection_step(aurora_protection_ctx_t *ctx,
                            const aurora_measurement_t *sample,
                            const aurora_charge_profile_t *profile,
                            uint32_t now_ms)
{
    const uint32_t required_measurements = AURORA_MEAS_VALID_PV_V |
                                           AURORA_MEAS_VALID_PV_I |
                                           AURORA_MEAS_VALID_BAT_V |
                                           AURORA_MEAS_VALID_BUS_V;

    if ((ctx == NULL) || (sample == NULL) || (profile == NULL))
    {
        return;
    }

    if ((sample->sequence != 0U) &&
        ((sample->valid_mask & required_measurements) == required_measurements))
    {
        ctx->measurement_seen = true;
    }

    if (!ctx->measurement_seen)
    {
        /* 上电宽限只允许等待首个完整块，超时后锁存ADC_STALE。 */
        if ((now_ms - ctx->startup_ms) > AURORA_MEASUREMENT_STARTUP_GRACE_MS)
        {
            latch_fault(ctx, AURORA_FAULT_ADC_STALE, now_ms);
        }
        return;
    }

    if (((sample->valid_mask & required_measurements) != required_measurements) ||
        ((now_ms - sample->timestamp_ms) > AURORA_MEASUREMENT_STALE_MS))
    {
        latch_fault(ctx, AURORA_FAULT_ADC_STALE, now_ms);
        return;
    }
    ctx->active_mask &= (uint32_t)~AURORA_FAULT_ADC_STALE;

    /* 无光/低PV属于待机工况，不作为必须人工清除的欠压故障。 */
    ctx->pv_uv_count = 0U;
    ctx->active_mask &= (uint32_t)~AURORA_FAULT_PV_UNDERVOLT;

    debounce_fault(ctx,
                   sample->pv_voltage_mv > AURORA_PV_ABSOLUTE_MAX_MV,
                   &ctx->pv_ov_count,
                   AURORA_FAULT_PV_OVERVOLT,
                   now_ms);

    /* 未接电池时不锁欠压；只有电池电压越过接入门槛后才检查档案下限。 */
    debounce_fault(ctx,
                   (sample->battery_voltage_mv > AURORA_BATTERY_CONNECTED_MIN_MV) &&
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
        const bool mos_valid =
            (sample->valid_mask & AURORA_MEAS_VALID_MOS_TEMP) != 0U;
        const bool mos_active =
            (ctx->active_mask & AURORA_FAULT_MOS_OVERTEMP) != 0U;
        const int16_t limit_dC = mos_active ?
                                     (int16_t)AURORA_MOS_RECOVER_TEMP_DC :
                                     (int16_t)AURORA_MOS_TRIP_TEMP_DC;
        const bool mos_fault = mos_valid && (sample->mos_temp_dC > limit_dC);

        if (!mos_valid)
        {
            /* MOS NTC无效时立即锁存，防止开路/短路被误认为低温而继续发波。 */
            latch_fault(ctx, AURORA_FAULT_MOS_TEMP_INVALID, now_ms);
        }
        else
        {
            /* 温度恢复有效只撤销传感器无效active，latched仍需显式清除。 */
            ctx->active_mask &= (uint32_t)~AURORA_FAULT_MOS_TEMP_INVALID;

            /* active期间使用恢复阈值形成迟滞，避免温度在边界附近反复抖动。 */
            debounce_fault(ctx,
                           mos_fault,
                           &ctx->mos_temp_count,
                           AURORA_FAULT_MOS_OVERTEMP,
                           now_ms);
        }
    }

    {
        const bool ambient_valid =
            (sample->valid_mask & AURORA_MEAS_VALID_AMB_TEMP) != 0U;
        const bool ambient_active =
            (ctx->active_mask & AURORA_FAULT_AMB_TEMP) != 0U;
        const int16_t low_limit_dC = ambient_active ?
                                           (int16_t)(AURORA_AMB_MIN_TEMP_DC +
                                                     AURORA_AMB_RECOVER_MARGIN_DC) :
                                           (int16_t)AURORA_AMB_MIN_TEMP_DC;
        const int16_t high_limit_dC = ambient_active ?
                                            (int16_t)(AURORA_AMB_MAX_TEMP_DC -
                                                      AURORA_AMB_RECOVER_MARGIN_DC) :
                                            (int16_t)AURORA_AMB_MAX_TEMP_DC;
        const bool ambient_fault =
            ambient_valid &&
            ((sample->ambient_temp_dC < low_limit_dC) ||
             (sample->ambient_temp_dC > high_limit_dC));

        debounce_fault(ctx,
                       ambient_fault,
                       &ctx->amb_temp_count,
                       AURORA_FAULT_AMB_TEMP,
                       now_ms);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
 *               uint32_t clear_mask, bool hardware_sources_inactive)
 * Input       : ctx - 保护上下文；clear_mask - 请求清除的锁存位；
 *               hardware_sources_inactive - 比较器/Break等硬件源已确认失效
 * Output      : true - 指定位已清除；false - 硬件源仍有效、active未恢复或参数错误
 * Description : 只允许清除已恢复的锁存位，并递增epoch使清除前取得的所有发波授权失效。
 *---------------------------------------------------------------------------*/
bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
                             uint32_t clear_mask,
                             bool hardware_sources_inactive)
{
    if ((ctx == NULL) || !hardware_sources_inactive ||
        ((ctx->active_mask & clear_mask) != 0U))
    {
        return false;
    }

    ctx->latched_mask &= ~clear_mask;
    ctx->epoch++;
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx)
 * Input       : ctx - 保护上下文
 * Output      : true - active与latched均为0；false - 参数无效或存在任一故障
 * Description : 提供给功率状态机和Service的统一软件安全判定，不代表板级硬件门禁已通过。
 *---------------------------------------------------------------------------*/
bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) &&
           (ctx->active_mask == 0U) &&
           (ctx->latched_mask == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx)
 * Input       : ctx - 保护上下文
 * Output      : 当前软件安全epoch；参数无效时返回0
 * Description : Service在“检查安全”与“执行发波”之间复核该值，阻止TOCTOU窗口中的旧授权。
 *---------------------------------------------------------------------------*/
uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx)
{
    return (ctx != NULL) ? ctx->epoch : 0U;
}
