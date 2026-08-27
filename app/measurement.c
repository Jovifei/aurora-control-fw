#include "measurement.h"

#include <limits.h>
#include <string.h>

/* DMA 扫描顺序必须与 driver/drv_adc.c 中的硬件序列完全一致。 */
enum
{
    ADC_IDX_PV_I = 0,
    ADC_IDX_PV_V,
    ADC_IDX_BAT_V,
    ADC_IDX_BUS_V,
    ADC_IDX_MOS_TEMP,
    ADC_IDX_AMB_TEMP
};

static int32_t clamp_i32(int64_t value)
{
    if (value > INT32_MAX)
    {
        return INT32_MAX;
    }
    if (value < INT32_MIN)
    {
        return INT32_MIN;
    }
    return (int32_t)value;
}

static uint16_t trimmed_average(const uint16_t *raw, size_t channel)
{
    uint32_t sum = 0U;
    uint16_t min_value = UINT16_MAX;
    uint16_t max_value = 0U;
    size_t scan;

    for (scan = 0U; scan < AURORA_ADC_SCANS_PER_BLOCK; ++scan)
    {
        const uint16_t value = raw[(scan * AURORA_ADC_CHANNEL_COUNT) + channel];
        sum += value;
        if (value < min_value)
        {
            min_value = value;
        }
        if (value > max_value)
        {
            max_value = value;
        }
    }

    /* 去掉一个最大值和一个最小值，减弱单次开关尖峰对控制量的影响。 */
    sum -= min_value;
    sum -= max_value;
    return (uint16_t)(sum / (AURORA_ADC_SCANS_PER_BLOCK - 2U));
}

static bool convert_channel(const aurora_adc_calibration_t *cal,
                            uint16_t raw,
                            int32_t *physical)
{
    int64_t value;

    if ((cal == NULL) || (physical == NULL) || !cal->valid || (cal->gain_den == 0))
    {
        return false;
    }

    value = ((int64_t)((int32_t)raw - (int32_t)cal->zero_code) *
             (int64_t)cal->gain_num) /
            (int64_t)cal->gain_den;
    value *= (int64_t)cal->polarity;
    value += cal->offset;
    *physical = clamp_i32(value);
    return true;
}

void aurora_measurement_init(aurora_measurement_ctx_t *ctx,
                             const aurora_measurement_calibration_t *calibration)
{
    if (ctx == NULL)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    if (calibration != NULL)
    {
        ctx->calibration = *calibration;
    }
    ctx->latest.battery_current_quality = AURORA_MEAS_QUALITY_INVALID;
}

aurora_status_t aurora_measurement_process_block(aurora_measurement_ctx_t *ctx,
                                                  const uint16_t *raw,
                                                  size_t word_count,
                                                  uint32_t timestamp_ms)
{
    aurora_measurement_t next;
    uint16_t average[AURORA_ADC_CHANNEL_COUNT];
    int32_t value;
    size_t channel;

    if ((ctx == NULL) || (raw == NULL) || (word_count != AURORA_ADC_BLOCK_WORDS))
    {
        return AURORA_STATUS_INVALID;
    }

    memset(&next, 0, sizeof(next));
    next.timestamp_ms = timestamp_ms;
    next.sequence = ++ctx->publish_sequence;
    next.battery_current_quality = AURORA_MEAS_QUALITY_INVALID;

    for (channel = 0U; channel < AURORA_ADC_CHANNEL_COUNT; ++channel)
    {
        average[channel] = trimmed_average(raw, channel);
    }

    if (convert_channel(&ctx->calibration.channel[ADC_IDX_PV_I], average[ADC_IDX_PV_I], &value))
    {
        next.pv_current_ma = value;
        next.valid_mask |= AURORA_MEAS_VALID_PV_I;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_PV_V], average[ADC_IDX_PV_V], &value))
    {
        next.pv_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_PV_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_BAT_V], average[ADC_IDX_BAT_V], &value))
    {
        next.battery_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_BAT_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_BUS_V], average[ADC_IDX_BUS_V], &value))
    {
        next.bus_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_BUS_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_MOS_TEMP], average[ADC_IDX_MOS_TEMP], &value))
    {
        next.mos_temp_dC = (int16_t)value;
        next.valid_mask |= AURORA_MEAS_VALID_MOS_TEMP;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_AMB_TEMP], average[ADC_IDX_AMB_TEMP], &value))
    {
        next.ambient_temp_dC = (int16_t)value;
        next.valid_mask |= AURORA_MEAS_VALID_AMB_TEMP;
    }

    if ((next.valid_mask & (AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I)) ==
        (AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I))
    {
        int64_t power = ((int64_t)next.pv_voltage_mv * (int64_t)next.pv_current_ma) / 1000LL;
        if (power < 0LL)
        {
            power = 0LL;
        }
        next.pv_power_mw = clamp_i32(power);
        next.valid_mask |= AURORA_MEAS_VALID_PV_POWER;
    }

    /* 单写者发布：主循环完成全部字段后再整体替换，ISR绝不写物理量快照。 */
    ctx->latest = next;
    return AURORA_STATUS_OK;
}

bool aurora_measurement_read(const aurora_measurement_ctx_t *ctx,
                             aurora_measurement_t *out)
{
    uint32_t before;
    uint32_t after;

    if ((ctx == NULL) || (out == NULL))
    {
        return false;
    }

    /* 允许通信/控制在主循环不同位置读取；序号变化时重读，避免撕裂快照。 */
    do
    {
        before = ctx->latest.sequence;
        *out = ctx->latest;
        after = ctx->latest.sequence;
    } while (before != after);

    return out->sequence != 0U;
}

void aurora_measurement_estimate_battery_current(aurora_measurement_t *sample,
                                                 uint16_t efficiency_q15,
                                                 bool relay_closed,
                                                 bool transient)
{
    int64_t battery_power_mw;

    if (sample == NULL)
    {
        return;
    }

    sample->valid_mask &= (uint32_t)~AURORA_MEAS_VALID_BAT_I_EST;
    sample->battery_current_est_ma = 0;
    sample->battery_current_quality = AURORA_MEAS_QUALITY_INVALID;

    if (!relay_closed || transient ||
        ((sample->valid_mask & (AURORA_MEAS_VALID_PV_POWER | AURORA_MEAS_VALID_BAT_V)) !=
         (AURORA_MEAS_VALID_PV_POWER | AURORA_MEAS_VALID_BAT_V)) ||
        (sample->battery_voltage_mv < 1000))
    {
        return;
    }

    battery_power_mw = ((int64_t)sample->pv_power_mw * efficiency_q15) /
                       (int64_t)AURORA_DUTY_Q15_ONE;
    sample->battery_current_est_ma = clamp_i32((battery_power_mw * 1000LL) /
                                               sample->battery_voltage_mv);
    sample->battery_current_quality = AURORA_MEAS_QUALITY_ESTIMATED;
    sample->valid_mask |= AURORA_MEAS_VALID_BAT_I_EST;
}
