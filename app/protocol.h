#ifndef AURORA_PROTOCOL_H
#define AURORA_PROTOCOL_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AURORA_PROTOCOL_RESOURCE_USER_DATA   (10001U)
#define AURORA_PROTOCOL_RESOURCE_SETTING     (10002U)
#define AURORA_PROTOCOL_RESOURCE_RESET       (10003U)

typedef struct
{
    uint8_t action;
    uint16_t resource;
    uint32_t message_id;
    uint16_t data_length;
    uint8_t data[AURORA_PROTOCOL_MAX_DATA];
} aurora_protocol_frame_t;

typedef struct
{
    uint8_t step;
    uint16_t item;
    uint16_t length;
    uint8_t checksum;
    aurora_protocol_frame_t frame;
    bool frame_ready;
    uint32_t last_byte_ms;
    uint32_t error_count;
} aurora_protocol_ctx_t;

void aurora_protocol_init(aurora_protocol_ctx_t *ctx);
void aurora_protocol_feed_byte(aurora_protocol_ctx_t *ctx,
                               uint8_t byte,
                               uint32_t now_ms);
bool aurora_protocol_take_frame(aurora_protocol_ctx_t *ctx,
                                aurora_protocol_frame_t *out);
size_t aurora_protocol_encode(const aurora_protocol_frame_t *frame,
                              uint8_t *wire,
                              size_t capacity);
void aurora_protocol_fill_telemetry(aurora_protocol_frame_t *frame,
                                    uint32_t message_id,
                                    const aurora_measurement_t *sample,
                                    aurora_charge_state_t charge_state,
                                    uint32_t fault_mask,
                                    const aurora_persistent_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif
