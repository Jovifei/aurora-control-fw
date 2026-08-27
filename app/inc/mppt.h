#ifndef AURORA_MPPT_H
#define AURORA_MPPT_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* MPPT外层工作状态。 */
typedef enum
{
    AURORA_MPPT_DISABLED = 0,                        /* 输入或权限无效，不输出功率请求。 */
    AURORA_MPPT_FAST_DESCENT,                        /* 从运行时Voc附近快速向MPP方向下降。 */
    AURORA_MPPT_TRACKING,                            /* 按P-V斜率正常搜索。 */
    AURORA_MPPT_LIMITED                              /* 外部限功率时冻结/跟随参考。 */
} aurora_mppt_state_t;

/* 参考电压型MPPT上下文。 */
typedef struct
{
    int64_t integral_mw;                             /* PV电压PI积分项，mW。 */
    uint32_t target_voltage_mv;                      /* 当前PV参考电压，mV。 */
    uint32_t open_circuit_voltage_mv;                /* 最近一次运行时Voc估计，mV。 */
    int32_t previous_voltage_mv;                     /* 上次P-V搜索使用的平均电压。 */
    int32_t previous_power_mw;                       /* 上次P-V搜索使用的平均功率。 */
    uint32_t last_search_ms;                         /* 上次外层P-V搜索时间。 */
    uint32_t last_pi_ms;                             /* 上次参考电压PI时间。 */
    aurora_mppt_state_t state;                       /* 当前搜索状态。 */
    bool previous_valid;                             /* 是否已有可比较的上一窗口。 */
    uint8_t state_reserved[6];                       /* 显式补齐状态字节，避免64位对象尾部隐式填充。 */
} aurora_mppt_ctx_t;

void aurora_mppt_init(aurora_mppt_ctx_t *ctx);
void aurora_mppt_reset(aurora_mppt_ctx_t *ctx);
void aurora_mppt_set_open_circuit_voltage(aurora_mppt_ctx_t *ctx, uint32_t voc_mv);
aurora_mppt_output_t aurora_mppt_step(aurora_mppt_ctx_t *ctx,
                                      const aurora_measurement_t *sample,
                                      uint32_t power_allow_mw,
                                      bool external_limited,
                                      uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif
