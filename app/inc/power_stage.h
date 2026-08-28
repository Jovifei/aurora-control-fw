#ifndef AURORA_POWER_STAGE_H
#define AURORA_POWER_STAGE_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单路异步Boost启动、预充、继电器和运行状态。 */
typedef struct
{
    int64_t power_integral;                          /* 功率执行器积分项，Q15。 */
    uint32_t state_since_ms;                         /* 当前状态进入时间。 */
    uint32_t pv_valid_since_ms;                      /* PV持续>=13V的起点，兼作100ms启动资格与2s零点稳定依据。 */
    uint32_t pv_fast_valid_since_ms;                 /* PV持续>=15V的起点，用于动态1~10s快速启动资格。 */
    uint32_t delta_ok_since_ms;                      /* BST_U/BAT_U压差持续满足起点。 */
    uint32_t no_sun_since_ms;                        /* 真正无PV持续起点。 */
    uint32_t bat_stability_since_ms;                 /* 10s电池稳定窗口起点。 */
    uint32_t start_success_since_ms;                 /* Ibat_est>=80mA成功启动计时。 */
    uint32_t selected_start_delay_ms;                /* 本次1~10s或15s启动等待。 */
    uint32_t dynamic_start_delay_ms;                 /* >15V启动的自适应1~10s延时。 */
    int32_t bat_stability_min_mv;                    /* 10s窗口BAT_U最小值。 */
    int32_t bat_stability_max_mv;                    /* 10s窗口BAT_U最大值。 */
    uint16_t duty_q15;                               /* 最近一次物理占空比命令。 */
    aurora_power_state_t state;                      /* 当前启动/预充/运行状态。 */
    bool relay_closed;                               /* 软件期望继电器状态。 */
    bool startup_success_recorded;                   /* 本轮成功后只减少一次启动延时。 */
    uint8_t state_reserved[2];                       /* 显式补齐。 */
} aurora_power_stage_ctx_t;

void aurora_power_stage_init(aurora_power_stage_ctx_t *ctx, uint32_t now_ms);
aurora_power_command_t aurora_power_stage_step(aurora_power_stage_ctx_t *ctx,
                                               const aurora_measurement_t *sample,
                                               const aurora_mppt_output_t *mppt,
                                               const aurora_charge_output_t *charger,
                                               bool protection_safe,
                                               bool zero_cal_ready,
                                               bool zero_cal_failed,
                                               uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
