#ifndef AURORA_PROTECTION_H
#define AURORA_PROTECTION_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Protection内部定时条件数量；语义索引只在protection.c内定义。 */
#define AURORA_PROTECTION_TIMER_COUNT               (30U)

/* 单个真实毫秒条件定时器，不再用“执行N次”等价时间。 */
typedef struct
{
    uint32_t since_ms;                               /* 条件首次连续成立时间。 */
    bool timing;                                     /* true表示当前正在连续计时。 */
    uint8_t reserved[3];                             /* 显式补齐32位对齐。 */
} aurora_condition_timer_t;

/* 软件保护、故障锁存与授权代际上下文。 */
typedef struct
{
    uint32_t active_mask;                            /* 当前仍阻止功率输出的故障位。 */
    uint32_t latched_mask;                           /* 必须延时/显式清除的历史故障位。 */
    uint32_t epoch;                                  /* 故障集合变化时递增，阻断旧发波授权。 */
    uint32_t first_fault_ms;                         /* 本轮首个故障出现时间。 */
    uint32_t startup_ms;                             /* 保护模块初始化时间。 */
    aurora_condition_timer_t timer[AURORA_PROTECTION_TIMER_COUNT]; /* 各保护独立定时。 */
    bool measurement_seen;                          /* 至少收到过一个完整V/I/Vbat/Vbus块。 */
    uint8_t measurement_seen_reserved[3];            /* 显式补齐。 */
} aurora_protection_ctx_t;

void aurora_protection_init(aurora_protection_ctx_t *ctx, uint32_t now_ms);
void aurora_protection_latch_fast_fault(aurora_protection_ctx_t *ctx,
                                        uint32_t fault_mask,
                                        uint32_t now_ms);
void aurora_protection_step(aurora_protection_ctx_t *ctx,
                            const aurora_measurement_t *sample,
                            const aurora_charge_profile_t *profile,
                            bool boost_output_active,
                            uint32_t now_ms);
bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
                             uint32_t clear_mask,
                             bool hardware_sources_inactive);
bool aurora_protection_clear_verified_fast_fault(aurora_protection_ctx_t *ctx,
                                                  uint32_t clear_mask,
                                                  bool hardware_sources_inactive);
bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx);
uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx);
uint32_t aurora_protection_fault_mask(const aurora_protection_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
