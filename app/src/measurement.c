#include "measurement.h"

#include "app_config.h"

#include <limits.h>
#include <string.h>

/* 去极值平均会从每个通道的数据块中移除一个最大值和一个最小值。 */
#define MEASUREMENT_TRIMMED_EXTREME_COUNT           (2U)
/* 最终原理图NTC网络的固定电阻，Ω；沿用120W成熟100K/B3950换算模型。 */
#define MEASUREMENT_NTC_PULL_OHM                    (5100UL)
/* 12位ADC阻值公式使用4096作为分母基数。 */
#define MEASUREMENT_ADC_CODE_BASE                   (4096UL)
/* 旧工程100K/B3950查表范围：-40~125°C，共166点。 */
#define MEASUREMENT_NTC_TABLE_SIZE                  (166U)

/* DMA扫描顺序必须与driver/src/drv_adc.c中的规则组序列完全一致。 */
typedef enum
{
    ADC_IDX_PV_I = 0,
    ADC_IDX_PV_V,
    ADC_IDX_BAT_V,
    ADC_IDX_BUS_V,
    ADC_IDX_MOS_TEMP,
    ADC_IDX_AMB_TEMP
} measurement_adc_index_t;

/*
 * 100K NTC、B=3950、-40~125°C阻值表。
 * 数值直接继承120W ADC.c 的 TabNtc_100K，避免重构时悄然改变温度曲线。
 */
static const uint32_t k_ntc_100k_ohm[MEASUREMENT_NTC_TABLE_SIZE] =
{
    0x34AF4CUL, 0x314105UL, 0x2E11EFUL, 0x2B1D16UL, 0x285DEEUL, 0x25D051UL, 0x23706EUL, 0x213AC8UL,
    0x1F2C2AUL, 0x1D41A3UL, 0x1B787FUL, 0x19CE43UL, 0x1840A6UL, 0x16CD8DUL, 0x15730BUL, 0x142F57UL,
    0x130026UL, 0x11E4B6UL, 0x10DB9EUL, 0xFE392UL, 0xEFB5EUL, 0xE21E6UL, 0xD5624UL, 0xC9725UL,
    0xBE40AUL, 0xB3C01UL, 0xA9E4BUL, 0xA0A36UL, 0x97F1DUL, 0x8FC66UL, 0x88184UL, 0x80DF2UL,
    0x7A137UL, 0x73AE0UL, 0x6DA83UL, 0x67FC0UL, 0x62A1BUL, 0x5D964UL, 0x58D4AUL, 0x54583UL,
    0x501CBUL, 0x4C1F1UL, 0x485A5UL, 0x44CAFUL, 0x416DBUL, 0x3E3F9UL, 0x3B3C4UL, 0x3862CUL,
    0x35B0BUL, 0x3323BUL, 0x30B98UL, 0x2E707UL, 0x2C464UL, 0x2A38FUL, 0x28470UL, 0x266ECUL,
    0x24AEAUL, 0x23058UL, 0x2171FUL, 0x1FF2EUL, 0x1E871UL, 0x1D2D7UL, 0x1BE51UL, 0x1AAD0UL,
    0x19843UL, 0x186A0UL, 0x175D8UL, 0x165E1UL, 0x156AEUL, 0x14835UL, 0x13A6DUL, 0x12D4DUL,
    0x120CCUL, 0x114E1UL, 0x10985UL, 0xFEAFUL, 0xF45AUL, 0xEA7EUL, 0xE115UL, 0xD81AUL,
    0xCF87UL, 0xC756UL, 0xBF84UL, 0xB80BUL, 0xB0E5UL, 0xAA11UL, 0xA38BUL, 0x9D4EUL,
    0x9757UL, 0x91A1UL, 0x8C2AUL, 0x86EFUL, 0x81ECUL, 0x7D1FUL, 0x7888UL, 0x7421UL,
    0x6FE9UL, 0x6BDDUL, 0x67FCUL, 0x6445UL, 0x60B3UL, 0x5D49UL, 0x5A02UL, 0x56DDUL,
    0x53D8UL, 0x50F1UL, 0x4E27UL, 0x4B79UL, 0x48E5UL, 0x466CUL, 0x440BUL, 0x41C4UL,
    0x3F94UL, 0x3D7AUL, 0x3B74UL, 0x3981UL, 0x37A0UL, 0x35D1UL, 0x3414UL, 0x3267UL,
    0x30CAUL, 0x2F3DUL, 0x2DBFUL, 0x2C4FUL, 0x2AECUL, 0x2996UL, 0x284CUL, 0x270FUL,
    0x25DCUL, 0x24B4UL, 0x2397UL, 0x2284UL, 0x217AUL, 0x207AUL, 0x1F82UL, 0x1E94UL,
    0x1DAEUL, 0x1CD0UL, 0x1BFAUL, 0x1B2BUL, 0x1A63UL, 0x19A1UL, 0x18E6UL, 0x1832UL,
    0x1783UL, 0x16DAUL, 0x1636UL, 0x1598UL, 0x14FFUL, 0x146BUL, 0x13DCUL, 0x1351UL,
    0x12CBUL, 0x1249UL, 0x11CBUL, 0x1151UL, 0x10DCUL, 0x106AUL, 0xFFBUL, 0xF91UL,
    0xF29UL, 0xEC5UL, 0xE64UL, 0xE05UL, 0xDAAUL, 0xD51UL
};

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
 * Description : 对同一通道16次扫描做去极值平均，降低开关尖峰对慢速控制量的影响。
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
 * Output      : true表示完成有效换算；false表示标定或输出参数无效
 * Description : 按零点、比例、极性和偏移换算电压/电流通道。
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
 * Name        : static int16_t ntc_temperature_dC(uint16_t raw)
 * Input       : raw - NTC通道12位ADC平均码
 * Output      : 温度，0.1°C；超表范围分别返回-41.0°C或126.0°C
 * Description : 沿用120W的5.1k分压和100K/B3950查表，并在线性插值到0.1°C。
 *---------------------------------------------------------------------------*/
static int16_t ntc_temperature_dC(uint16_t raw)
{
    uint32_t resistance;
    size_t low;
    size_t high;

    if (raw >= 4095U)
    {
        return -410;
    }

    resistance = (uint32_t)(((uint64_t)raw * MEASUREMENT_NTC_PULL_OHM) /
                            ((uint64_t)MEASUREMENT_ADC_CODE_BASE - raw));
    if (resistance >= k_ntc_100k_ohm[0])
    {
        return -410;
    }
    if (resistance <= k_ntc_100k_ohm[MEASUREMENT_NTC_TABLE_SIZE - 1U])
    {
        return 1260;
    }

    low = 0U;
    high = MEASUREMENT_NTC_TABLE_SIZE - 1U;
    while ((high - low) > 1U)
    {
        const size_t middle = (low + high) / 2U;
        if (resistance > k_ntc_100k_ohm[middle])
        {
            high = middle;
        }
        else
        {
            low = middle;
        }
    }

    {
        const uint32_t upper_r = k_ntc_100k_ohm[low];
        const uint32_t lower_r = k_ntc_100k_ohm[low + 1U];
        const uint32_t span = upper_r - lower_r;
        const uint32_t offset = upper_r - resistance;
        const int32_t base_dC = -400 + (int32_t)(low * 10U);
        const int32_t fraction_dC = (span != 0U) ?
                                        (int32_t)((offset * 10U) / span) : 0;
        return (int16_t)(base_dC + fraction_dC);
    }
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_measurement_init(aurora_measurement_ctx_t *ctx,
 *               const aurora_measurement_calibration_t *calibration)
 * Input       : ctx - 测量上下文；calibration - 六通道板级标定参数
 * Output      : 无
 * Description : 清零测量状态并复制标定；BAT_I不存在硬件通道，初始明确为INVALID。
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
 * Input       : ctx - 测量上下文；raw - 完整DMA块；word_count - 块内字数；
 *               timestamp_ms - DMA块完成时间戳
 * Output      : OK表示快照已发布；INVALID表示参数或块长度错误
 * Description : 完成六通道去极值平均、V/I标定、NTC查表、PV功率计算和快照发布。
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

    if (convert_channel(&ctx->calibration.channel[ADC_IDX_PV_I],
                        average[ADC_IDX_PV_I], &value))
    {
        next.pv_current_ma = value;
        next.valid_mask |= AURORA_MEAS_VALID_PV_I;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_PV_V],
                        average[ADC_IDX_PV_V], &value))
    {
        next.pv_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_PV_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_BAT_V],
                        average[ADC_IDX_BAT_V], &value))
    {
        next.battery_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_BAT_V;
    }
    if (convert_channel(&ctx->calibration.channel[ADC_IDX_BUS_V],
                        average[ADC_IDX_BUS_V], &value))
    {
        next.bus_voltage_mv = value;
        next.valid_mask |= AURORA_MEAS_VALID_BUS_V;
    }

    next.mos_temp_dC = ntc_temperature_dC(average[ADC_IDX_MOS_TEMP]);
    next.ambient_temp_dC = ntc_temperature_dC(average[ADC_IDX_AMB_TEMP]);
    next.valid_mask |= AURORA_MEAS_VALID_MOS_TEMP | AURORA_MEAS_VALID_AMB_TEMP;

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

    ctx->latest = next;
    return AURORA_STATUS_OK;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_measurement_read(const aurora_measurement_ctx_t *ctx,
 *               aurora_measurement_t *out)
 * Input       : ctx - 测量上下文；out - 快照输出地址
 * Output      : true表示已读取至少一帧测量；false表示尚无数据或参数错误
 * Description : 通过序号前后复核读取一致快照，避免读到整体替换过程中的撕裂数据。
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
 * Input       : sample - 测量快照；efficiency_q15 - 功率级效率估计；
 *               relay_closed - 继电器闭合；transient - 继电器/母线瞬态
 * Output      : 无
 * Description : 无BAT_I硬件时用Ppv×效率/Vbat估算充电电流，并明确标记ESTIMATED。
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

/*---------------------------------------------------------------------------*
 * Name        : void aurora_measurement_zero_cal_reset(aurora_measurement_ctx_t *ctx)
 * Input       : ctx - 测量上下文
 * Output      : 无
 * Description : 清除本轮PV_I运行时零点累计；不改写已经发布的测量快照。
 *---------------------------------------------------------------------------*/
void aurora_measurement_zero_cal_reset(aurora_measurement_ctx_t *ctx)
{
    if (ctx != NULL)
    {
        ctx->zero_cal_sum = 0U;
        ctx->zero_cal_blocks = 0U;
        ctx->zero_cal_ready = false;
        ctx->zero_cal_failed = false;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_status_t aurora_measurement_zero_cal_accumulate(
 *               aurora_measurement_ctx_t *ctx, const uint16_t *raw,
 *               size_t word_count)
 * Input       : ctx - 测量上下文；raw - PWM关闭时的完整DMA块；word_count - 块字数
 * Output      : OK表示校准完成；BUSY表示继续累计；INVALID表示参数或零点越界
 * Description : 累计32个完整PV_I块并更新运行时zero_code，避免理论2048码直接用于量产。
 *---------------------------------------------------------------------------*/
aurora_status_t aurora_measurement_zero_cal_accumulate(aurora_measurement_ctx_t *ctx,
                                                        const uint16_t *raw,
                                                        size_t word_count)
{
    uint32_t zero_code;

    if ((ctx == NULL) || (raw == NULL) || (word_count != AURORA_ADC_BLOCK_WORDS))
    {
        return AURORA_STATUS_INVALID;
    }
    if (ctx->zero_cal_ready)
    {
        return AURORA_STATUS_OK;
    }
    if (ctx->zero_cal_failed)
    {
        return AURORA_STATUS_INVALID;
    }

    ctx->zero_cal_sum += trimmed_average(raw, ADC_IDX_PV_I);
    ctx->zero_cal_blocks++;
    if (ctx->zero_cal_blocks < AURORA_ZERO_CAL_BLOCKS)
    {
        return AURORA_STATUS_BUSY;
    }

    zero_code = ctx->zero_cal_sum / ctx->zero_cal_blocks;
    if ((zero_code < AURORA_ZERO_CAL_CODE_MIN) ||
        (zero_code > AURORA_ZERO_CAL_CODE_MAX))
    {
        ctx->zero_cal_failed = true;
        return AURORA_STATUS_INVALID;
    }

    ctx->calibration.channel[ADC_IDX_PV_I].zero_code = (int16_t)zero_code;
    ctx->zero_cal_ready = true;
    return AURORA_STATUS_OK;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_measurement_zero_cal_ready(const aurora_measurement_ctx_t *ctx)
 * Input       : ctx - 测量上下文
 * Output      : true表示PV_I运行时零点已经完成
 * Description : 供Power Stage启动状态机读取校准门禁，不触发新的采样。
 *---------------------------------------------------------------------------*/
bool aurora_measurement_zero_cal_ready(const aurora_measurement_ctx_t *ctx)
{
    return (ctx != NULL) && ctx->zero_cal_ready;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_measurement_zero_cal_failed(const aurora_measurement_ctx_t *ctx)
 * Input       : ctx - 测量上下文
 * Output      : true表示本轮零点码超出候选安全窗口
 * Description : 零点异常时上层保持功率关闭并重新走启动/诊断流程。
 *---------------------------------------------------------------------------*/
bool aurora_measurement_zero_cal_failed(const aurora_measurement_ctx_t *ctx)
{
    return (ctx != NULL) && ctx->zero_cal_failed;
}
