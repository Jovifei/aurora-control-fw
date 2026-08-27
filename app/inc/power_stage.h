#ifndef AURORA_POWER_STAGE_H
#define AURORA_POWER_STAGE_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单路异步Boost功率级状态与控制器记忆。 */
typedef struct
{
    aurora_power_state_t state;                      /* 当前预充/继电器/运行状态。 */
    uint32_t state_since_ms;                         /* 当前状态进入时间。 */
    uint32_t delta_ok_since_ms;                      /* 母线压差持续满足时间起点。 */
    uint32_t no_sun_since_ms;                        /* 弱光持续时间起点。 */
    uint16_t duty_q15;                               /* 最近一次Q6物理占空比命令。 */
    int64_t power_integral;                          /* 功率执行器积分项，Q15。 */
    bool relay_closed;                               /* 软件认定的继电器状态。 */
} aurora_power_stage_ctx_t;

void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx, uint32_t now_ms);
aurora_power_command_t aurora_power_stage_step(aurora_power_stage_ctx_t *ctx,
                                               const aurora_measurement_t *sample,
                                               const aurora_mppt_output_t *mppt,
                                               const aurora_charge_output_t *charger,
                                               bool protection_safe,
                                               uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
