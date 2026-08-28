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
    aurora_charge_profile_t base_profile;            /* V2.7的25°C基础档案，永不被温补永久改写。 */
    aurora_charge_profile_t profile;                 /* 当前生效档案；铅酸会按环境温度动态补偿。 */
    uint32_t state_since_ms;                         /* 当前阶段进入时间。 */
    uint32_t charge_start_ms;                        /* 本轮充电开始时间。 */
    uint32_t tail_since_ms;                          /* CV尾流/Float低流持续条件起点。 */
    uint32_t float_start_ms;                         /* 真正开始Float功率控制的时间。 */
    uint32_t transition_since_ms;                    /* TC→CC、CV→CC、Float入口等连续条件起点。 */
    uint32_t float_low_voltage_since_ms;             /* Float低于下限的连续时间。 */
    uint32_t recharge_since_ms;                      /* Complete后低于复充阈值的连续时间。 */
    uint16_t cc_to_cv_score;                         /* 继承120W的加权CC→CV证据积分。 */
    aurora_charge_state_t state;                     /* 当前TC/CC/CV/Float阶段。 */
    bool initialized;                                /* 档案有效且状态机已初始化。 */
    bool float_started;                              /* true表示电池已自然下降到Float窗口并重新开始浮充。 */
    uint8_t state_reserved[2];                       /* 显式补齐布尔字段。 */
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
