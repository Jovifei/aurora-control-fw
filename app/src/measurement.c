#include "measurement.h"

#include "app_config.h"

#include <limits.h>
#include <string.h>

/* 去极值平均会从每个通道的数据块中移除一个最大值和一个最小值。 */
#define MEASUREMENT_TRIMMED_EXTREME_COUNT          (2U)

/* DMA扫描顺序必须与driver/drv_adc.c中的规则组序列完全一致。 */
typedef enum
{
    ADC_IDX_PV_I = 0,
    ADC_IDX_PV_V,
    ADC_IDX_BAT_V,
    ADC_IDX_BUS_V,
    ADC_IDX_MOS_TEMP,
    ADC_IDX_AMB_TEMP
} measurement_adc_index_t;

/*---------------------------------------------------------------------------*
 * Name        : static int32_t clamp_i32(int64_t value)
 * Input       : value - 64位有符号中间结果
 * Output      : 饱和到int32_t范围的结果
 * Description : 防止标定乘除、功率计算或电流估算超出应用层32位物理量范围。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : static uint16_t trimmed_average(const uint16_t *raw, size_t channel)
 * Input       : raw - 完整ADC DMA数据块；channel - 逻辑通道索引
 * Output      : 去掉单个最大/最小值后的平均ADC码
 * Description : 对同一通道的16次扫描做去极值平均，降低单次开关尖峰对慢速控制量的影响。
 *---------------------------------------------------------------------------*/
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

    sum -= min_value;
    sum -= max_value;
    return (uint16_t)(sum /
                      (AURORA_ADC_SCANS_PER_BLOCK - MEASUREMENT_TRIMMED_EXTREME_COUNT));
}

/*---------------------------------------------------------------------------*
 * Name        : static bool convert_channel(const aurora_adc_calibration_t *cal,
 *               uint16_t raw, int32_t *physical)
 * Input       : cal - 通道标定参数；raw - 经过滤波的ADC码；physical - 物理量输出地址
 * Output      : true - 已完成有效换算；false - 标定或输出参数无效
 * Description : 按零点、比例、极性和偏移换算物理量；标定无效时不发布该通道有效位。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : void aurora_measurement_init(aurora_measurement_ctx_t *ctx,
 *               const aurora_measurement_calibration_t *calibration)
 * Input       : ctx - 测量模块上下文；calibration - 六通道板级标定参数
 * Output      : 无
 * Description : 清零测量状态并复制标定；BAT_I不存在硬件通道，因此估算质量初始明确为INVALID。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : aurora_status_t aurora_measurement_process_block(
 *               aurora_measurement_ctx_t *ctx, const uint16_t *raw,
 *               size_t word_count, uint32_t timestamp_ms)
 * Input       : ctx - 测量模块上下文；raw - 完整DMA块；word_count - 块内16位字数；
 *               timestamp_ms - DMA块完成时间戳
 * Output      : AURORA_STATUS_OK - 快照已发布；AURORA_STATUS_INVALID - 参数或块长度错误
 * Description : 完成六通道去极值平均、标定换算、PV功率计算和单写者快照发布。
 *---------------------------------------------------------------------------*/
aurora_status_t aurora_measurement_process_block(aurora_measurement_ctx_t *ctx,
                                                  const uint16_t *raw,
                                                  size_t word_count,
                                                  uint32_t timestamp_ms)
{
    aurora_measurement_t next;
    uint16_t average[AURORA_ADC_CHANNEL_COUNT];
    int32_t value;
    size_t channel;

    /* 只接受驱动发布的完整六通道数据块，拒绝部分块覆盖旧快照。 */
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

    /* 每个通道独立发布有效位，未标定的温度通道不会伪装成有效数据。 */
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_PV_I],
                        average[ADC_IDX_PV_I],
                        &value))
    {
        next.pv_current_ma = value;
        next.valid_mask |= AURORA_MEAS_VALID_PV_I;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_PV_V],
                        average[ADC_IDX_PV_V],
                        &value))
    {
        next.pv_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_PV_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_BAT_V],
                        average[ADC_IDX_BAT_V],
                        &value))
    {
        next.battery_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_BAT_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_BUS_V],
                        average[ADC_IDX_BUS_V],
                        &value))
    {
        next.bus_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_BUS_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_MOS_TEMP],
                        average[ADC_IDX_MOS_TEMP],
                        &value))
    {
        next.mos_temp_dC = (int16_t)value;
        next.valid_mask |= AURORA_MEAS_VALID_MOS_TEMP;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_AMB_TEMP],
                        average[ADC_IDX_AMB_TEMP],
                        &value))
    {
        next.ambient_temp_dC = (int16_t)value;
        next.valid_mask |= AURORA_MEAS_VALID_AMB_TEMP;
    }

    /* 只有Vpv和Ipv同时有效时才计算功率，负功率按本单向充电器语义钳为0。 */
    if ((next.valid_mask & (AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I)) ==
        (AURORA_MEAS_VALID_PV_V | AURORA_MEAS_VALID_PV_I))
    {
        int64_t power_mw =
            ((int64_t)next.pv_voltage_mv * (int64_t)next.pv_current_ma) /
            (int64_t)AURORA_MV_MA_PER_MW;

        if (power_mw < 0LL)
        {
            power_mw = 0LL;
        }
        next.pv_power_mw = clamp_i32(power_mw);
        next.valid_mask |= AURORA_MEAS_VALID_PV_POWER;
    }

    /* ISR只发布DMA块；物理量快照由主循环单写者在字段完整后整体替换。 */
    ctx->latest = next;
    return AURORA_STATUS_OK;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_measurement_read(const aurora_measurement_ctx_t *ctx,
 *               aurora_measurement_t *out)
 * Input       : ctx - 测量模块上下文；out - 快照输出地址
 * Output      : true - 已读取至少一帧测量；false - 参数错误或尚未发布数据
 * Description : 通过序号前后复核读取一致快照，避免读到主循环整体替换过程中的撕裂数据。
 *---------------------------------------------------------------------------*/
bool aurora_measurement_read(const aurora_measurement_ctx_t *ctx,
                             aurora_measurement_t *out)
{
    uint32_t sequence_before;
    uint32_t sequence_after;

    if ((ctx == NULL) || (out == NULL))
    {
        return false;
    }

    do
    {
        sequence_before = ctx->latest.sequence;
        *out = ctx->latest;
        sequence_after = ctx->latest.sequence;
    } while (sequence_before != sequence_after);

    return out->sequence != 0U;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_measurement_estimate_battery_current(
 *               aurora_measurement_t *sample, uint16_t efficiency_q15,
 *               bool relay_closed, bool transient)
 * Input       : sample - 可更新的测量快照；efficiency_q15 - 功率级效率估计；
 *               relay_closed - 继电器闭合标志；transient - 继电器瞬态标志
 * Output      : 无
 * Description : 在无BAT_I硬件通道时用Ppv×效率/Vbat估算充电电流，并显式标记为ESTIMATED而非实测。
 *---------------------------------------------------------------------------*/
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

    /* 每次先撤销旧估算，只有本周期条件全部成立时才重新发布有效位。 */
    sample->valid_mask &= (uint32_t)~AURORA_MEAS_VALID_BAT_I_EST;
    sample->battery_current_est_ma = 0;
    sample->battery_current_quality = AURORA_MEAS_QUALITY_INVALID;

    if (!relay_closed || transient ||
        ((sample->valid_mask & (AURORA_MEAS_VALID_PV_POWER | AURORA_MEAS_VALID_BAT_V)) !=
         (AURORA_MEAS_VALID_PV_POWER | AURORA_MEAS_VALID_BAT_V)) ||
        (sample->battery_voltage_mv < AURORA_BATTERY_ESTIMATE_MIN_MV))
    {
        return;
    }

    battery_power_mw = ((int64_t)sample->pv_power_mw * efficiency_q15) /
                       (int64_t)AURORA_DUTY_Q15_ONE;
    sample->battery_current_est_ma =
        clamp_i32((battery_power_mw * (int64_t)AURORA_MV_MA_PER_MW) /
                  sample->battery_voltage_mv);
    sample->battery_current_quality = AURORA_MEAS_QUALITY_ESTIMATED;
    sample->valid_mask |= AURORA_MEAS_VALID_BAT_I_EST;
}
