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
    uint16_t zero_cal_blocks;                        /* 已累计完整DMA块数量。 */
    bool zero_cal_ready;                             /* true表示运行时PV_I零点已校准。 */
    bool zero_cal_failed;                            /* true表示零点码超出候选安全窗口。 */
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
