#ifndef AURORA_MEASUREMENT_H
#define AURORA_MEASUREMENT_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ADC完整块处理与运行时PV_I零点标定上下文。 */
typedef struct
{
    aurora_measurement_calibration_t calibration;    /* 每个逻辑通道的板级标定。 */
    aurora_measurement_t latest;                     /* 最近一次完整发布快照。 */
    uint32_t publish_sequence;                       /* 发布序号，便于检测新数据。 */
    uint32_t zero_cal_sum;                           /* PV_I零点校准DMA块平均码累计。 */
    uint32_t last_temp_filter_ms;                    /* 上次温度方向滤波评估时间。 */
    uint16_t zero_cal_blocks;                        /* 当前连续稳定窗口内的有效DMA块数量。 */
    uint16_t zero_cal_attempt_blocks;                /* 本轮已观察的DMA块总数，含被拒绝的不稳定块。 */
    uint16_t zero_cal_min_code;                      /* 本轮零点块平均码最小值。 */
    uint16_t zero_cal_max_code;                      /* 本轮零点块平均码最大值。 */
    int16_t mos_filtered_dC;                         /* MOS方向滤波输出。 */
    int16_t ambient_filtered_dC;                     /* 环境方向滤波输出。 */
    uint8_t mos_rise_count;                          /* MOS连续升温确认次数。 */
    uint8_t mos_fall_count;                          /* MOS连续降温确认次数。 */
    uint8_t ambient_rise_count;                      /* 环境连续升温确认次数。 */
    uint8_t ambient_fall_count;                      /* 环境连续降温确认次数。 */
    bool mos_temp_initialized;                       /* MOS滤波是否已有初值。 */
    bool ambient_temp_initialized;                   /* 环境滤波是否已有初值。 */
    bool zero_cal_ready;                             /* true表示运行时PV_I零点已校准。 */
    bool zero_cal_failed;                            /* true表示零点均值/抖动超出候选安全窗口。 */
} aurora_measurement_ctx_t;

void aurora_measurement_init(aurora_measurement_ctx_t *ctx,
                             const aurora_measurement_calibration_t *calibration);
aurora_status_t aurora_measurement_process_block(aurora_measurement_ctx_t *ctx,
                                                  const uint16_t *raw,
                                                  size_t word_count,
                                                  uint32_t timestamp_ms);
bool aurora_measurement_read(const aurora_measurement_ctx_t *ctx,
                             aurora_measurement_t *out);
void aurora_measurement_estimate_battery_current(aurora_measurement_t *sample,
                                                 uint16_t efficiency_q15,
                                                 bool relay_closed,
                                                 bool transient);
void aurora_measurement_zero_cal_reset(aurora_measurement_ctx_t *ctx);
aurora_status_t aurora_measurement_zero_cal_accumulate(aurora_measurement_ctx_t *ctx,
                                                        const uint16_t *raw,
                                                        size_t word_count);
bool aurora_measurement_zero_cal_ready(const aurora_measurement_ctx_t *ctx);
bool aurora_measurement_zero_cal_failed(const aurora_measurement_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
