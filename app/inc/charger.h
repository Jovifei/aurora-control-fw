#ifndef AURORA_CHARGER_H
#define AURORA_CHARGER_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 电池充电状态机上下文。 */
typedef struct
{
    int64_t cv_integral_mw;                          /* CV电压环积分项，电池侧mW。 */
    int64_t cc_integral_mw;                          /* 无BAT_I硬件时的估算电流修正积分，mW。 */
    aurora_charge_profile_t profile;                 /* 当前化学体系、电压档位和保护档案。 */
    uint32_t state_since_ms;                         /* 当前阶段进入时间。 */
    uint32_t charge_start_ms;                        /* 本轮充电开始时间。 */
    uint32_t tail_since_ms;                          /* 尾流持续满足条件的起点。 */
    uint32_t float_start_ms;                         /* 铅酸浮充开始时间。 */
    aurora_charge_state_t state;                     /* 当前TC/CC/CV/Float阶段。 */
    bool initialized;                                /* 档案有效且状态机已初始化。 */
    uint8_t state_reserved[6];                       /* 显式补齐到32位边界，避免AC6隐式尾部填充。 */
} aurora_charger_ctx_t;

bool aurora_charge_profile_get(aurora_battery_chem_t chemistry,
                               aurora_battery_pack_t pack,
                               aurora_charge_profile_t *out);
void aurora_charger_init(aurora_charger_ctx_t *ctx,
                         aurora_battery_chem_t chemistry,
                         aurora_battery_pack_t pack,
                         uint32_t now_ms);
aurora_charge_output_t aurora_charger_step(aurora_charger_ctx_t *ctx,
                                           const aurora_measurement_t *sample,
                                           bool weak_light,
                                           bool thermal_limited,
                                           bool input_limited,
                                           uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
