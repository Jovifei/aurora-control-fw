#ifndef AURORA_MEASUREMENT_H
#define AURORA_MEASUREMENT_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    aurora_measurement_calibration_t calibration;
    aurora_measurement_t latest;
    uint32_t publish_sequence;
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

#ifdef __cplusplus
}
#endif

#endif
