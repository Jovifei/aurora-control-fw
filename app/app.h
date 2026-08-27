#ifndef AURORA_APP_H
#define AURORA_APP_H

#include "charger.h"
#include "measurement.h"
#include "mppt.h"
#include "power_stage.h"
#include "protection.h"
#include "protocol.h"
#include "storage.h"
#include "ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    aurora_measurement_ctx_t measurement;
    aurora_mppt_ctx_t mppt;
    aurora_charger_ctx_t charger;
    aurora_protection_ctx_t protection;
    aurora_power_stage_ctx_t power_stage;
    aurora_ui_ctx_t ui;
    aurora_protocol_ctx_t protocol;
    aurora_storage_ctx_t storage;
    aurora_measurement_t sample;
    aurora_mppt_output_t mppt_output;
    aurora_charge_output_t charge_output;
    aurora_power_command_t power_command;
    aurora_ui_output_t ui_output;
    uint32_t last_step_ms;
    uint32_t last_10ms;
    uint32_t telemetry_message_id;
    uint64_t energy_accumulator_mw_ms;
} aurora_app_t;

void aurora_app_init(aurora_app_t *app,
                     const aurora_measurement_calibration_t *calibration,
                     uint32_t now_ms);
void aurora_app_apply_settings(aurora_app_t *app,
                               const aurora_persistent_settings_t *settings,
                               uint32_t now_ms);
void aurora_app_on_adc_block(aurora_app_t *app,
                             const uint16_t *raw,
                             size_t word_count,
                             uint32_t timestamp_ms);
void aurora_app_on_fast_fault(aurora_app_t *app,
                              uint32_t fault_mask,
                              uint32_t now_ms);
void aurora_app_step_1ms(aurora_app_t *app, uint32_t now_ms);
void aurora_app_on_protocol_frame(aurora_app_t *app,
                                  const aurora_protocol_frame_t *frame,
                                  aurora_protocol_frame_t *response,
                                  bool *has_response,
                                  uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
