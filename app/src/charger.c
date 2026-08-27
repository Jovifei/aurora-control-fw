#include "charger.h"

#include "app_config.h"

#include <string.h>

/*
 * 12组电池参数均使用物理单位，字段含义通过指定初始化器显式表达。
 * 这些值来自当前工程候选基线，尚未替代电芯/BMS规格书和台架验证；
 * 调整流程、证据要求及责任边界见 docs/17-参数标定与Codex交接清单.md。
 */
static const aurora_charge_profile_t k_profiles[AURORA_CHEM_COUNT][AURORA_PACK_COUNT] =
{
    [AURORA_CHEM_LEAD][AURORA_PACK_48V] =
    {
        .chemistry = AURORA_CHEM_LEAD,
        .pack = AURORA_PACK_48V,
        .battery_uv_mv = 41500U,
        .trickle_exit_mv = 48000U,
        .cv_target_mv = 58200U,
        .cv_protect_mv = 58800U,
        .float_target_mv = 54800U,
        .recharge_mv = 51200U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 150U
    },
    [AURORA_CHEM_LEAD][AURORA_PACK_60V] =
    {
        .chemistry = AURORA_CHEM_LEAD,
        .pack = AURORA_PACK_60V,
        .battery_uv_mv = 52000U,
        .trickle_exit_mv = 60000U,
        .cv_target_mv = 72700U,
        .cv_protect_mv = 73400U,
        .float_target_mv = 68500U,
        .recharge_mv = 64000U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 150U
    },
    [AURORA_CHEM_LEAD][AURORA_PACK_72V] =
    {
        .chemistry = AURORA_CHEM_LEAD,
        .pack = AURORA_PACK_72V,
        .battery_uv_mv = 62500U,
        .trickle_exit_mv = 72000U,
        .cv_target_mv = 87200U,
        .cv_protect_mv = 88000U,
        .float_target_mv = 82200U,
        .recharge_mv = 76800U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 150U
    },
    [AURORA_CHEM_TERNARY][AURORA_PACK_48V] =
    {
        .chemistry = AURORA_CHEM_TERNARY,
        .pack = AURORA_PACK_48V,
        .battery_uv_mv = 35100U,
        .trickle_exit_mv = 39000U,
        .cv_target_mv = 54600U,
        .cv_protect_mv = 56880U,
        .float_target_mv = 0U,
        .recharge_mv = 52650U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_TERNARY][AURORA_PACK_60V] =
    {
        .chemistry = AURORA_CHEM_TERNARY,
        .pack = AURORA_PACK_60V,
        .battery_uv_mv = 45900U,
        .trickle_exit_mv = 51000U,
        .cv_target_mv = 71400U,
        .cv_protect_mv = 74380U,
        .float_target_mv = 0U,
        .recharge_mv = 68850U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_TERNARY][AURORA_PACK_72V] =
    {
        .chemistry = AURORA_CHEM_TERNARY,
        .pack = AURORA_PACK_72V,
        .battery_uv_mv = 54000U,
        .trickle_exit_mv = 60000U,
        .cv_target_mv = 84000U,
        .cv_protect_mv = 87500U,
        .float_target_mv = 0U,
        .recharge_mv = 81000U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_LFP][AURORA_PACK_48V] =
    {
        .chemistry = AURORA_CHEM_LFP,
        .pack = AURORA_PACK_48V,
        .battery_uv_mv = 40000U,
        .trickle_exit_mv = 48000U,
        .cv_target_mv = 57600U,
        .cv_protect_mv = 60000U,
        .float_target_mv = 0U,
        .recharge_mv = 53600U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_LFP][AURORA_PACK_60V] =
    {
        .chemistry = AURORA_CHEM_LFP,
        .pack = AURORA_PACK_60V,
        .battery_uv_mv = 50000U,
        .trickle_exit_mv = 60000U,
        .cv_target_mv = 72000U,
        .cv_protect_mv = 75000U,
        .float_target_mv = 0U,
        .recharge_mv = 67000U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_LFP][AURORA_PACK_72V] =
    {
        .chemistry = AURORA_CHEM_LFP,
        .pack = AURORA_PACK_72V,
        .battery_uv_mv = 60000U,
        .trickle_exit_mv = 72000U,
        .cv_target_mv = 86400U,
        .cv_protect_mv = 90000U,
        .float_target_mv = 0U,
        .recharge_mv = 80400U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_SODIUM][AURORA_PACK_48V] =
    {
        .chemistry = AURORA_CHEM_SODIUM,
        .pack = AURORA_PACK_48V,
        .battery_uv_mv = 30600U,
        .trickle_exit_mv = 34000U,
        .cv_target_mv = 56100U,
        .cv_protect_mv = 58650U,
        .float_target_mv = 0U,
        .recharge_mv = 52700U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_SODIUM][AURORA_PACK_60V] =
    {
        .chemistry = AURORA_CHEM_SODIUM,
        .pack = AURORA_PACK_60V,
        .battery_uv_mv = 39600U,
        .trickle_exit_mv = 44000U,
        .cv_target_mv = 72600U,
        .cv_protect_mv = 75900U,
        .float_target_mv = 0U,
        .recharge_mv = 68200U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    },
    [AURORA_CHEM_SODIUM][AURORA_PACK_72V] =
    {
        .chemistry = AURORA_CHEM_SODIUM,
        .pack = AURORA_PACK_72V,
        .battery_uv_mv = 46800U,
        .trickle_exit_mv = 52000U,
        .cv_target_mv = 85800U,
        .cv_protect_mv = 89700U,
        .float_target_mv = 0U,
        .recharge_mv = 80600U,
        .trickle_current_ma = 1000U,
        .cc_current_ma = 3000U,
        .tail_current_ma = 300U,
        .float_current_ma = 0U
    }
};

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
 * Input       : now_ms - 当前毫秒时间戳；then_ms - 起始毫秒时间戳
 * Output      : 两个时间戳之间的无符号间隔，单位ms
 * Description : 使用无符号减法计算时间间隔，兼容32位毫秒计数器自然回绕。
 *---------------------------------------------------------------------------*/
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t current_to_power_limit(uint32_t voltage_mv, uint32_t current_ma)
 * Input       : voltage_mv - 电池端电压，单位mV；current_ma - 允许充电电流，单位mA
 * Output      : 受产品额定功率限制的功率上限，单位mW
 * Description : 用64位中间量完成V×I换算，并把结果限制在当前BOM额定功率内。
 *---------------------------------------------------------------------------*/
static uint32_t current_to_power_limit(uint32_t voltage_mv, uint32_t current_ma)
{
    uint64_t power_mw = ((uint64_t)voltage_mv * current_ma) / AURORA_MV_MA_PER_MW;

    if (power_mw > AURORA_RATED_POWER_MW)
    {
        power_mw = AURORA_RATED_POWER_MW;
    }
    return (uint32_t)power_mw;
}

/*---------------------------------------------------------------------------*
 * Name        : static void enter_state(aurora_charger_ctx_t *ctx, aurora_charge_state_t state,
 *               uint32_t now_ms)
 * Input       : ctx - 充电状态机上下文；state - 目标充电状态；now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 统一记录状态进入时间并清理尾流计时；进入Float时额外记录浮充起点。
 *---------------------------------------------------------------------------*/
static void enter_state(aurora_charger_ctx_t *ctx,
                        aurora_charge_state_t state,
                        uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->tail_since_ms = 0U;

    if (state == AURORA_CHARGE_FLOAT)
    {
        ctx->float_start_ms = now_ms;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t cv_power_limit(aurora_charger_ctx_t *ctx,
 *               const aurora_measurement_t *sample)
 * Input       : ctx - 充电状态机上下文；sample - 最新测量快照
 * Output      : CV阶段允许的功率上限，单位mW
 * Description : 依据电池电压误差执行CV PI与条件积分，防止饱和时继续累积并限制到额定功率。
 *---------------------------------------------------------------------------*/
static uint32_t cv_power_limit(aurora_charger_ctx_t *ctx,
                               const aurora_measurement_t *sample)
{
    const int32_t error_mv = (int32_t)ctx->profile.cv_target_mv -
                             sample->battery_voltage_mv;
    const int64_t proportional_mw =
        (int64_t)error_mv * AURORA_CHARGER_CV_KP_MW_PER_MV;
    int64_t candidate_integral_mw =
        ctx->cv_integral_mw +
        ((int64_t)error_mv * AURORA_CHARGER_CV_KI_MW_PER_MV_STEP);
    int64_t output_mw;

    /* 积分项本身只允许在0至额定功率之间变化。 */
    if (candidate_integral_mw < 0LL)
    {
        candidate_integral_mw = 0LL;
    }
    if (candidate_integral_mw > (int64_t)AURORA_RATED_POWER_MW)
    {
        candidate_integral_mw = AURORA_RATED_POWER_MW;
    }

    output_mw = proportional_mw + candidate_integral_mw;

    /* 输出饱和且误差仍把控制量推向饱和方向时，冻结积分。 */
    if (!(((output_mw >= (int64_t)AURORA_RATED_POWER_MW) && (error_mv > 0)) ||
          ((output_mw <= 0LL) && (error_mv < 0))))
    {
        ctx->cv_integral_mw = candidate_integral_mw;
    }

    if (output_mw < 0LL)
    {
        output_mw = 0LL;
    }
    if (output_mw > (int64_t)AURORA_RATED_POWER_MW)
    {
        output_mw = AURORA_RATED_POWER_MW;
    }
    return (uint32_t)output_mw;
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_charge_profile_get(aurora_battery_chem_t chemistry,
 *               aurora_battery_pack_t pack, aurora_charge_profile_t *out)
 * Input       : chemistry - 电池化学体系；pack - 48/60/72V档位；out - 档案输出地址
 * Output      : true - 档案有效并已复制；false - 参数为空或索引越界
 * Description : 从只读二维表读取指定电池档案，禁止越界索引进入充电状态机。
 *---------------------------------------------------------------------------*/
bool aurora_charge_profile_get(aurora_battery_chem_t chemistry,
                               aurora_battery_pack_t pack,
                               aurora_charge_profile_t *out)
{
    if ((out == NULL) || (chemistry >= AURORA_CHEM_COUNT) ||
        (pack >= AURORA_PACK_COUNT))
    {
        return false;
    }

    *out = k_profiles[chemistry][pack];
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : void aurora_charger_init(aurora_charger_ctx_t *ctx,
 *               aurora_battery_chem_t chemistry, aurora_battery_pack_t pack,
 *               uint32_t now_ms)
 * Input       : ctx - 充电状态机上下文；chemistry - 电池化学体系；pack - 电压档位；
 *               now_ms - 当前毫秒时间戳
 * Output      : 无
 * Description : 清零动态状态并装载电池档案；档案无效时直接进入FAULT，避免使用未定义参数。
 *---------------------------------------------------------------------------*/
void aurora_charger_init(aurora_charger_ctx_t *ctx,
                         aurora_battery_chem_t chemistry,
                         aurora_battery_pack_t pack,
                         uint32_t now_ms)
{
    if (ctx == NULL)
    {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->initialized = aurora_charge_profile_get(chemistry, pack, &ctx->profile);
    ctx->state = ctx->initialized ? AURORA_CHARGE_OFF : AURORA_CHARGE_FAULT;
    ctx->state_since_ms = now_ms;
    ctx->charge_start_ms = now_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_charge_output_t aurora_charger_step(aurora_charger_ctx_t *ctx,
 *               const aurora_measurement_t *sample, bool weak_light,
 *               bool thermal_limited, uint32_t now_ms)
 * Input       : ctx - 充电状态机上下文；sample - 最新测量快照；weak_light - 弱光标志；
 *               thermal_limited - 温度降额标志；now_ms - 当前毫秒时间戳
 * Output      : 当前充电状态、允许功率、目标电压和充电许可
 * Description : 推进TC/CC/CV/Float/Complete状态机；弱光或温度降额期间暂停尾流判满，避免误判满电。
 *---------------------------------------------------------------------------*/
aurora_charge_output_t aurora_charger_step(aurora_charger_ctx_t *ctx,
                                           const aurora_measurement_t *sample,
                                           bool weak_light,
                                           bool thermal_limited,
                                           uint32_t now_ms)
{
    aurora_charge_output_t output;
    const uint32_t required_measurements = AURORA_MEAS_VALID_BAT_V;

    memset(&output, 0, sizeof(output));

    /* 电池电压是所有充电阶段的最低数据前提；缺失时不得输出功率许可。 */
    if ((ctx == NULL) || (sample == NULL) || !ctx->initialized ||
        ((sample->valid_mask & required_measurements) != required_measurements))
    {
        output.state = AURORA_CHARGE_FAULT;
        return output;
    }

    /* 电池超过档案保护电压时立即进入软件FAULT，不再推进普通状态。 */
    if ((uint32_t)sample->battery_voltage_mv > ctx->profile.cv_protect_mv)
    {
        enter_state(ctx, AURORA_CHARGE_FAULT, now_ms);
    }

    /* TC/CC/CV/Float总时长超过上限时安全结束本次充电。 */
    if ((ctx->state != AURORA_CHARGE_OFF) &&
        (ctx->state != AURORA_CHARGE_COMPLETE) &&
        (ctx->state != AURORA_CHARGE_FAULT) &&
        (elapsed_ms(now_ms, ctx->charge_start_ms) >= AURORA_MAX_CHARGE_TIME_MS))
    {
        enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
    }

    switch (ctx->state)
    {
    case AURORA_CHARGE_OFF:
        /* 依据当前电池电压选择低压涓流或直接进入恒流。 */
        ctx->charge_start_ms = now_ms;
        ctx->cv_integral_mw = 0LL;
        if ((uint32_t)sample->battery_voltage_mv < ctx->profile.trickle_exit_mv)
        {
            enter_state(ctx, AURORA_CHARGE_TRICKLE, now_ms);
        }
        else
        {
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_TRICKLE:
        /* 低压电池恢复到涓流退出阈值后进入主充CC。 */
        if ((uint32_t)sample->battery_voltage_mv >= ctx->profile.trickle_exit_mv)
        {
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_CC:
        /* 到达CV目标后，用当前CC功率初始化积分，减小切换突变。 */
        if ((uint32_t)sample->battery_voltage_mv >= ctx->profile.cv_target_mv)
        {
            ctx->cv_integral_mw =
                current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                       ctx->profile.cc_current_ma);
            enter_state(ctx, AURORA_CHARGE_CV, now_ms);
        }
        break;

    case AURORA_CHARGE_CV:
        /* 电压显著跌离CV目标时退回CC，避免CV环在错误工作区长期饱和。 */
        if (((uint32_t)sample->battery_voltage_mv + AURORA_CHARGER_CV_RETURN_HYST_MV) <
            ctx->profile.cv_target_mv)
        {
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        else if (((sample->valid_mask & AURORA_MEAS_VALID_BAT_I_EST) != 0U) &&
                 !weak_light && !thermal_limited &&
                 (sample->battery_current_est_ma >= 0) &&
                 ((uint32_t)sample->battery_current_est_ma <= ctx->profile.tail_current_ma))
        {
            /* 尾流必须持续满足；铅酸转Float，其余化学体系直接Complete。 */
            if (ctx->tail_since_ms == 0U)
            {
                ctx->tail_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->tail_since_ms) >= AURORA_TAIL_HOLD_MS)
            {
                const aurora_charge_state_t next_state =
                    (ctx->profile.chemistry == AURORA_CHEM_LEAD) ?
                        AURORA_CHARGE_FLOAT : AURORA_CHARGE_COMPLETE;
                enter_state(ctx, next_state, now_ms);
            }
        }
        else
        {
            /* 云影或温度限功率会让估算电流下降，此时必须重新开始尾流计时。 */
            ctx->tail_since_ms = 0U;
        }
        break;

    case AURORA_CHARGE_FLOAT:
        /* 铅酸浮充到达最长时间后结束本次充电。 */
        if (elapsed_ms(now_ms, ctx->float_start_ms) >= AURORA_FLOAT_TIME_MS)
        {
            enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
        }
        break;

    case AURORA_CHARGE_COMPLETE:
        /* 电池自然回落到复充阈值后重新进入CC。 */
        if ((uint32_t)sample->battery_voltage_mv <= ctx->profile.recharge_mv)
        {
            ctx->charge_start_ms = now_ms;
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_FAULT:
        /* FAULT不在本模块内自动恢复，由保护清除和上层重新初始化决定。 */
        break;
    }

    output.state = ctx->state;
    output.weak_light = weak_light;
    output.power_limited = thermal_limited;
    output.voltage_target_mv = ctx->profile.cv_target_mv;

    /* 状态机只输出允许功率；MPPT和功率执行器仍会施加更严格的共同上限。 */
    switch (ctx->state)
    {
    case AURORA_CHARGE_TRICKLE:
        output.allow_charge = true;
        output.power_limit_mw =
            current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                   ctx->profile.trickle_current_ma);
        break;

    case AURORA_CHARGE_CC:
        output.allow_charge = true;
        output.power_limit_mw =
            current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                   ctx->profile.cc_current_ma);
        break;

    case AURORA_CHARGE_CV:
        output.allow_charge = true;
        output.power_limit_mw = cv_power_limit(ctx, sample);
        break;

    case AURORA_CHARGE_FLOAT:
        output.allow_charge = true;
        output.voltage_target_mv = ctx->profile.float_target_mv;
        output.power_limit_mw =
            current_to_power_limit((uint32_t)sample->battery_voltage_mv,
                                   ctx->profile.float_current_ma);
        break;

    case AURORA_CHARGE_OFF:
    case AURORA_CHARGE_COMPLETE:
    case AURORA_CHARGE_FAULT:
        output.allow_charge = false;
        output.power_limit_mw = 0U;
        break;
    }

    if (output.power_limit_mw > AURORA_RATED_POWER_MW)
    {
        output.power_limit_mw = AURORA_RATED_POWER_MW;
    }
    return output;
}
