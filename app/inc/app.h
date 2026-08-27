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

/* 应用层组合根：只保存业务状态，不直接包含寄存器或具体驱动对象。 */
typedef struct
{
    aurora_mppt_ctx_t mppt;                          /* PV参考电压搜索与PI状态。 */
    aurora_charger_ctx_t charger;                    /* 电池档案与TC/CC/CV/Float状态机。 */
    aurora_power_stage_ctx_t power_stage;            /* 预充、继电器和Duty执行器。 */
    uint64_t energy_accumulator_mw_ms;               /* 尚未折算为整Wh的能量余数。 */
    aurora_measurement_ctx_t measurement;            /* ADC数据处理与原子快照。 */
    aurora_protection_ctx_t protection;              /* 软件故障去抖、锁存和恢复状态。 */
    aurora_storage_ctx_t storage;                    /* 双页Flash Journal状态。 */
    aurora_protocol_ctx_t protocol;                  /* 旧产品UART协议解析状态。 */
    aurora_measurement_t sample;                     /* 最近一次可供控制使用的测量快照。 */
    uint32_t last_step_ms;                           /* 上一次1ms应用调度时间。 */
    uint32_t last_10ms;                              /* 上一次10ms控制链运行时间。 */
    uint32_t telemetry_message_id;                   /* 主动遥测消息序号。 */
    aurora_mppt_output_t mppt_output;                /* 最近一次MPPT输出。 */
    aurora_charge_output_t charge_output;            /* 最近一次充电状态机输出。 */
    aurora_power_command_t power_command;            /* 待Service落实的功率命令。 */
    aurora_ui_ctx_t ui;                              /* LED相位状态。 */
    aurora_ui_output_t ui_output;                    /* 待Service落实的LED命令。 */
    uint8_t layout_reserved[10];                     /* 显式补齐应用组合根，避免64位对齐产生隐式填充。 */
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
