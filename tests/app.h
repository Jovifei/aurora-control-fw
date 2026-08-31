#ifndef AURORA_TEST_APP_COMPAT_H
#define AURORA_TEST_APP_COMPAT_H

/* 仅供现有Host回归逐步迁移；生产代码不再存在app.h。 */
#include "main.h"

/*
 * 历史测试直接调用Battery兼容入口，并假定同一调用内完成关波复核。
 * v0.10.3生产路径由Runtime传递真实relay_applied；这里仅为旧测试补进20ms和一个新序号，
 * 新增tests/test_v0103.c直接调用step_ex验证真实HOLD_OFF行为。
 */
static inline aurora_power_command_t
aurora_test_power_stage_step(aurora_power_stage_ctx_t *ctx,
                             const aurora_measurement_t *sample,
                             const aurora_mppt_output_t *mppt,
                             const aurora_charge_output_t *charger,
                             bool protection_safe,
                             bool zero_cal_ready,
                             bool zero_cal_failed,
                             uint32_t now_ms)
{
    aurora_power_command_t command = aurora_power_stage_step_ex(
        ctx, sample, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true, ctx->relay_generation,
        AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV, AURORA_DEMO_POWER_LIMIT_MW, now_ms);

    if (command.state == AURORA_POWER_RELAY_HOLD_OFF)
    {
        aurora_measurement_t fresh = *sample;
        // 历史夹具没有Runtime，这里显式模拟“物理关PWM后记录基准”的生产握手。
        ctx->relay_holdoff_sequence = sample->sequence;
        ctx->state_since_ms = now_ms;
        fresh.sequence += AURORA_RELAY_POST_OFF_MIN_BLOCKS;
        fresh.timestamp_ms = now_ms + AURORA_RELAY_PWM_OFF_DECAY_MS;
        command = aurora_power_stage_step_ex(
            ctx, &fresh, mppt, charger, protection_safe, zero_cal_ready, zero_cal_failed, true, ctx->relay_generation,
            AURORA_MODE_BATTERY, AURORA_DEMO_TARGET_VOLTAGE_MV, AURORA_DEMO_POWER_LIMIT_MW,
            fresh.timestamp_ms);
        if (command.state == AURORA_POWER_RELAY_SETTLE)
        {
            ctx->delta_ok_since_ms = now_ms;
        }
    }
    return command;
}

#define aurora_power_stage_step aurora_test_power_stage_step /* 旧Host回归兼容入口。 */

#endif
