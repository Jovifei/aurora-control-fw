#ifndef AURORA_PROTECTION_H
#define AURORA_PROTECTION_H

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 软件保护、故障锁存与授权代际上下文。 */
typedef struct
{
    uint32_t active_mask;                            /* 当前条件仍成立的故障位。 */
    uint32_t latched_mask;                           /* 必须显式清除的历史故障位。 */
    uint32_t epoch;                                  /* 每次锁存/清除递增，阻断旧发波授权。 */
    uint32_t first_fault_ms;                         /* 本轮首个故障锁存时间。 */
    uint16_t pv_uv_count;                            /* PV欠压连续样本计数。 */
    uint16_t pv_ov_count;                            /* PV过压连续样本计数。 */
    uint16_t bat_uv_count;                           /* 电池欠压连续样本计数。 */
    uint16_t bat_ov_count;                           /* 电池过压连续样本计数。 */
    uint16_t mos_temp_count;                         /* MOS过温连续样本计数。 */
    uint16_t amb_temp_count;                         /* 环境温度异常连续样本计数。 */
    uint32_t startup_ms;                             /* 保护模块初始化时间。 */
    bool measurement_seen;                          /* 至少收到过一个有效测量块。 */
    uint8_t measurement_seen_reserved[3];            /* 显式补齐有效位后的字节，避免隐式填充。 */
} aurora_protection_ctx_t;

void aurora_protection_init(aurora_protection_ctx_t *ctx, uint32_t now_ms);
void aurora_protection_latch_fast_fault(aurora_protection_ctx_t *ctx,
                                        uint32_t fault_mask,
                                        uint32_t now_ms);
void aurora_protection_step(aurora_protection_ctx_t *ctx,
                            const aurora_measurement_t *sample,
                            const aurora_charge_profile_t *profile,
                            uint32_t now_ms);
bool aurora_protection_clear(aurora_protection_ctx_t *ctx,
                             uint32_t clear_mask,
                             bool hardware_sources_inactive);
bool aurora_protection_is_safe(const aurora_protection_ctx_t *ctx);
uint32_t aurora_protection_epoch(const aurora_protection_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
