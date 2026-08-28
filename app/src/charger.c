#include "charger.h"

#include "app_config.h"

#include <string.h>

/*
 * 12组充电档案按《青稞120W MPPT保护功能CheckList V2.7》重新录入。
 * 充电目标、验收上下限和软件保护阈值分开保存，避免把“最大允许值”误当控制目标。
 * 300W仅改变功率能力和硬件实现，不擅自改变电池化学体系的成熟充电电压。
 */
static const aurora_charge_profile_t k_profiles[AURORA_CHEM_COUNT][AURORA_PACK_COUNT] =
{
    [AURORA_CHEM_LEAD][AURORA_PACK_48V] =
    {
        .battery_uv_mv = 41500U, .battery_uv_recover_mv = 42000U,
        .trickle_exit_mv = 48000U,
        .cv_target_mv = 58000U, .cv_min_mv = 57800U, .cv_max_mv = 58200U,
        .ov_slow_mv = 58800U, .ov_medium_mv = 59500U,
        .ov_fast_mv = 61800U, .ov_absolute_mv = 93000U,
        .float_target_mv = 54600U, .float_min_mv = 54400U, .float_max_mv = 54800U,
        .full_voltage_mv = 58200U, .recharge_mv = 51200U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 80U,
        .chemistry = AURORA_CHEM_LEAD, .pack = AURORA_PACK_48V
    },
    [AURORA_CHEM_LEAD][AURORA_PACK_60V] =
    {
        .battery_uv_mv = 52000U, .battery_uv_recover_mv = 52500U,
        .trickle_exit_mv = 60000U,
        .cv_target_mv = 72500U, .cv_min_mv = 72300U, .cv_max_mv = 72700U,
        .ov_slow_mv = 73400U, .ov_medium_mv = 74100U,
        .ov_fast_mv = 76400U, .ov_absolute_mv = 93000U,
        .float_target_mv = 68300U, .float_min_mv = 68100U, .float_max_mv = 68500U,
        .full_voltage_mv = 72700U, .recharge_mv = 64000U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 80U,
        .chemistry = AURORA_CHEM_LEAD, .pack = AURORA_PACK_60V
    },
    [AURORA_CHEM_LEAD][AURORA_PACK_72V] =
    {
        .battery_uv_mv = 62500U, .battery_uv_recover_mv = 63000U,
        .trickle_exit_mv = 72000U,
        .cv_target_mv = 87000U, .cv_min_mv = 86800U, .cv_max_mv = 87200U,
        .ov_slow_mv = 88000U, .ov_medium_mv = 88700U,
        .ov_fast_mv = 91000U, .ov_absolute_mv = 93000U,
        .float_target_mv = 82000U, .float_min_mv = 81800U, .float_max_mv = 82200U,
        .full_voltage_mv = 87200U, .recharge_mv = 76800U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 80U,
        .chemistry = AURORA_CHEM_LEAD, .pack = AURORA_PACK_72V
    },

    [AURORA_CHEM_TERNARY][AURORA_PACK_48V] =
    {
        .battery_uv_mv = 35100U, .battery_uv_recover_mv = 35600U,
        .trickle_exit_mv = 39000U,
        .cv_target_mv = 54600U, .cv_min_mv = 54400U, .cv_max_mv = 54800U,
        .ov_slow_mv = 56875U, .ov_medium_mv = 57575U,
        .ov_fast_mv = 61800U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 54600U, .recharge_mv = 52650U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_TERNARY, .pack = AURORA_PACK_48V
    },
    [AURORA_CHEM_TERNARY][AURORA_PACK_60V] =
    {
        .battery_uv_mv = 45900U, .battery_uv_recover_mv = 46400U,
        .trickle_exit_mv = 51000U,
        .cv_target_mv = 71400U, .cv_min_mv = 71200U, .cv_max_mv = 71600U,
        .ov_slow_mv = 74375U, .ov_medium_mv = 75075U,
        .ov_fast_mv = 76400U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 71400U, .recharge_mv = 68850U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_TERNARY, .pack = AURORA_PACK_60V
    },
    [AURORA_CHEM_TERNARY][AURORA_PACK_72V] =
    {
        .battery_uv_mv = 54000U, .battery_uv_recover_mv = 54500U,
        .trickle_exit_mv = 60000U,
        .cv_target_mv = 84000U, .cv_min_mv = 83800U, .cv_max_mv = 84200U,
        .ov_slow_mv = 87500U, .ov_medium_mv = 88200U,
        .ov_fast_mv = 91000U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 84000U, .recharge_mv = 81000U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_TERNARY, .pack = AURORA_PACK_72V
    },

    [AURORA_CHEM_LFP][AURORA_PACK_48V] =
    {
        .battery_uv_mv = 40000U, .battery_uv_recover_mv = 40500U,
        .trickle_exit_mv = 48000U,
        .cv_target_mv = 57600U, .cv_min_mv = 57400U, .cv_max_mv = 57800U,
        .ov_slow_mv = 60000U, .ov_medium_mv = 60700U,
        .ov_fast_mv = 61800U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 57600U, .recharge_mv = 53600U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_LFP, .pack = AURORA_PACK_48V
    },
    [AURORA_CHEM_LFP][AURORA_PACK_60V] =
    {
        .battery_uv_mv = 50000U, .battery_uv_recover_mv = 50500U,
        .trickle_exit_mv = 60000U,
        .cv_target_mv = 72000U, .cv_min_mv = 71800U, .cv_max_mv = 72200U,
        .ov_slow_mv = 75000U, .ov_medium_mv = 75700U,
        .ov_fast_mv = 76400U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 72000U, .recharge_mv = 67000U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_LFP, .pack = AURORA_PACK_60V
    },
    [AURORA_CHEM_LFP][AURORA_PACK_72V] =
    {
        .battery_uv_mv = 60000U, .battery_uv_recover_mv = 60500U,
        .trickle_exit_mv = 72000U,
        .cv_target_mv = 86400U, .cv_min_mv = 86200U, .cv_max_mv = 86600U,
        .ov_slow_mv = 90000U, .ov_medium_mv = 90700U,
        .ov_fast_mv = 91000U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 86400U, .recharge_mv = 80400U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_LFP, .pack = AURORA_PACK_72V
    },

    [AURORA_CHEM_SODIUM][AURORA_PACK_48V] =
    {
        .battery_uv_mv = 30600U, .battery_uv_recover_mv = 31100U,
        .trickle_exit_mv = 34000U,
        .cv_target_mv = 56100U, .cv_min_mv = 55900U, .cv_max_mv = 56300U,
        .ov_slow_mv = 58650U, .ov_medium_mv = 59350U,
        .ov_fast_mv = 61800U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 56100U, .recharge_mv = 52700U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_SODIUM, .pack = AURORA_PACK_48V
    },
    [AURORA_CHEM_SODIUM][AURORA_PACK_60V] =
    {
        .battery_uv_mv = 39600U, .battery_uv_recover_mv = 40100U,
        .trickle_exit_mv = 44000U,
        .cv_target_mv = 72600U, .cv_min_mv = 72400U, .cv_max_mv = 72800U,
        .ov_slow_mv = 75900U, .ov_medium_mv = 76600U,
        .ov_fast_mv = 76400U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 72600U, .recharge_mv = 68200U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_SODIUM, .pack = AURORA_PACK_60V
    },
    [AURORA_CHEM_SODIUM][AURORA_PACK_72V] =
    {
        .battery_uv_mv = 46800U, .battery_uv_recover_mv = 47300U,
        .trickle_exit_mv = 52000U,
        .cv_target_mv = 85800U, .cv_min_mv = 85600U, .cv_max_mv = 86000U,
        .ov_slow_mv = 89700U, .ov_medium_mv = 90400U,
        .ov_fast_mv = 91000U, .ov_absolute_mv = 93000U,
        .float_target_mv = 0U, .float_min_mv = 0U, .float_max_mv = 0U,
        .full_voltage_mv = 85800U, .recharge_mv = 80600U,
        .trickle_current_ma = 1000U, .cc_current_ma = 3000U,
        .tail_current_ma = 300U, .float_end_current_ma = 0U,
        .chemistry = AURORA_CHEM_SODIUM, .pack = AURORA_PACK_72V
    }
};

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
 * Input       : now_ms - 当前毫秒；then_ms - 起始毫秒
 * Output      : 无符号时间间隔，ms
 * Description : 使用无符号减法兼容32位毫秒计数器自然回绕。
 *---------------------------------------------------------------------------*/
static uint32_t elapsed_ms(uint32_t now_ms, uint32_t then_ms)
{
    return now_ms - then_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t clamp_power(int64_t power_mw)
 * Input       : power_mw - 电池侧功率中间量，mW
 * Output      : 0~当前BOM额定功率范围内的mW
 * Description : 统一限制充电器PI和前馈输出，避免负功率或越过硬件额定值。
 *---------------------------------------------------------------------------*/
static uint32_t clamp_power(int64_t power_mw)
{
    if (power_mw <= 0LL)
    {
        return 0U;
    }
    if (power_mw >= (int64_t)AURORA_RATED_POWER_MW)
    {
        return AURORA_RATED_POWER_MW;
    }
    return (uint32_t)power_mw;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t current_feedforward_mw(uint32_t voltage_mv,
 *               uint32_t current_ma)
 * Input       : voltage_mv - 电池电压，mV；current_ma - 目标电流，mA
 * Output      : 电池侧前馈功率，mW
 * Description : 用64位中间量计算Vbat×Ibat，不把该电池侧值直接当成PV输入功率。
 *---------------------------------------------------------------------------*/
static uint32_t current_feedforward_mw(uint32_t voltage_mv, uint32_t current_ma)
{
    const uint64_t power_mw =
        ((uint64_t)voltage_mv * current_ma) / AURORA_MV_MA_PER_MW;
    return (power_mw > AURORA_RATED_POWER_MW) ?
               AURORA_RATED_POWER_MW : (uint32_t)power_mw;
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t lead_cell_count(aurora_battery_pack_t pack)
 * Input       : pack - 48/60/72V平台
 * Output      : 2V铅酸单格数
 * Description : 旧120W温补按每个2V cell计算；48/60/72V分别按24/30/36格。
 *---------------------------------------------------------------------------*/
static uint32_t lead_cell_count(aurora_battery_pack_t pack)
{
    switch (pack)
    {
    case AURORA_PACK_48V: return 24U;
    case AURORA_PACK_60V: return 30U;
    case AURORA_PACK_72V: return 36U;
    default: return 0U;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t add_mv_clamped(uint32_t base_mv, int64_t delta_mv)
 * Input       : base_mv - 基础电压；delta_mv - 有符号补偿量
 * Output      : 非负32位电压
 * Description : 统一处理铅酸温补，避免负补偿与无符号量混算。
 *---------------------------------------------------------------------------*/
static uint32_t add_mv_clamped(uint32_t base_mv, int64_t delta_mv)
{
    int64_t value = (int64_t)base_mv + delta_mv;
    if (value <= 0LL)
    {
        return 0U;
    }
    return (value > UINT32_MAX) ? UINT32_MAX : (uint32_t)value;
}

/*---------------------------------------------------------------------------*
 * Name        : static void update_effective_profile(aurora_charger_ctx_t *ctx,
 *               const aurora_measurement_t *sample)
 * Input       : ctx - 充电上下文；sample - 最新测量
 * Output      : 无
 * Description : 继承120W铅酸25°C/-3mV°C/cell温补；锂/钠保持基础档案。正温补受固定Fast OV安全裕量钳制。
 *---------------------------------------------------------------------------*/
static void update_effective_profile(aurora_charger_ctx_t *ctx,
                                     const aurora_measurement_t *sample)
{
    int32_t temp_dC;
    uint32_t cells;
    int64_t delta_mv;

    ctx->profile = ctx->base_profile;
    if ((ctx->base_profile.chemistry != AURORA_CHEM_LEAD) ||
        ((sample->valid_mask & AURORA_MEAS_VALID_AMB_TEMP) == 0U) ||
        (sample->ambient_ntc_status != AURORA_NTC_STATUS_OK))
    {
        return;
    }

    temp_dC = sample->ambient_temp_dC;
    if (temp_dC < AURORA_LEAD_TEMP_COMP_MIN_DC)
    {
        temp_dC = AURORA_LEAD_TEMP_COMP_MIN_DC;
    }
    if (temp_dC > AURORA_LEAD_TEMP_COMP_MAX_DC)
    {
        temp_dC = AURORA_LEAD_TEMP_COMP_MAX_DC;
    }

    cells = lead_cell_count(ctx->base_profile.pack);
    delta_mv = ((int64_t)(temp_dC - AURORA_LEAD_TEMP_COMP_REFERENCE_DC) *
                AURORA_LEAD_TEMP_COMP_MV_PER_C_PER_CELL * cells) / 10LL;

    if (delta_mv > 0LL)
    {
        const int64_t fast_safe_max =
            (int64_t)ctx->base_profile.ov_fast_mv - AURORA_LEAD_TEMP_COMP_FAST_OV_GUARD_MV;
        const int64_t max_positive = fast_safe_max - ctx->base_profile.cv_max_mv;
        if (max_positive < 0LL)
        {
            delta_mv = 0LL;
        }
        else if (delta_mv > max_positive)
        {
            delta_mv = max_positive;
        }
    }

    ctx->profile.cv_target_mv = add_mv_clamped(ctx->base_profile.cv_target_mv, delta_mv);
    ctx->profile.cv_min_mv = add_mv_clamped(ctx->base_profile.cv_min_mv, delta_mv);
    ctx->profile.cv_max_mv = add_mv_clamped(ctx->base_profile.cv_max_mv, delta_mv);
    ctx->profile.float_target_mv = add_mv_clamped(ctx->base_profile.float_target_mv, delta_mv);
    ctx->profile.float_min_mv = add_mv_clamped(ctx->base_profile.float_min_mv, delta_mv);
    ctx->profile.float_max_mv = add_mv_clamped(ctx->base_profile.float_max_mv, delta_mv);
    ctx->profile.full_voltage_mv = add_mv_clamped(ctx->base_profile.full_voltage_mv, delta_mv);
    ctx->profile.ov_slow_mv = add_mv_clamped(ctx->base_profile.ov_slow_mv, delta_mv);
    ctx->profile.ov_medium_mv = add_mv_clamped(ctx->base_profile.ov_medium_mv, delta_mv);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool condition_held(uint32_t *since_ms, bool condition,
 *               uint32_t hold_ms, uint32_t now_ms)
 * Input       : since_ms - 连续条件起点；condition - 当前条件；hold_ms - 目标时间；now_ms - 当前时间
 * Output      : true表示条件已连续成立足够久
 * Description : 充电状态转移使用真实毫秒而不是一次ADC越阈值就立即切换。
 *---------------------------------------------------------------------------*/
static bool condition_held(uint32_t *since_ms, bool condition,
                           uint32_t hold_ms, uint32_t now_ms)
{
    if (!condition)
    {
        *since_ms = 0U;
        return false;
    }
    if (*since_ms == 0U)
    {
        *since_ms = now_ms;
        return hold_ms == 0U;
    }
    return (now_ms - *since_ms) >= hold_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : static void enter_state(aurora_charger_ctx_t *ctx,
 *               aurora_charge_state_t state, uint32_t now_ms)
 * Input       : ctx - 充电上下文；state - 目标阶段；now_ms - 当前毫秒
 * Output      : 无
 * Description : 统一记录阶段时间并清尾流计时；进入Float时记录浮充起点。
 *---------------------------------------------------------------------------*/
static void enter_state(aurora_charger_ctx_t *ctx,
                        aurora_charge_state_t state,
                        uint32_t now_ms)
{
    ctx->state = state;
    ctx->state_since_ms = now_ms;
    ctx->tail_since_ms = 0U;
    ctx->transition_since_ms = 0U;
    ctx->float_low_voltage_since_ms = 0U;
    ctx->recharge_since_ms = 0U;
    ctx->cc_to_cv_score = 0U;
    if (state == AURORA_CHARGE_FLOAT)
    {
        /* 120W成熟逻辑：先停止充电，等电池自然降到Float窗口中点，再真正启动Float。 */
        ctx->float_started = false;
        ctx->float_start_ms = 0U;
    }
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t current_power_target(aurora_charger_ctx_t *ctx,
 *               const aurora_measurement_t *sample, uint32_t target_current_ma)
 * Input       : ctx - 充电上下文；sample - 测量快照；target_current_ma - 电池目标电流
 * Output      : 电池侧目标功率，mW
 * Description : 无BAT_I硬件时以Vbat×Itarget前馈，并用BAT_I_EST做低带宽PI修正。
 *---------------------------------------------------------------------------*/
static uint32_t current_power_target(aurora_charger_ctx_t *ctx,
                                     const aurora_measurement_t *sample,
                                     uint32_t target_current_ma)
{
    const uint32_t feedforward_mw =
        current_feedforward_mw((uint32_t)sample->battery_voltage_mv, target_current_ma);
    int64_t output_mw = feedforward_mw;

    if (((sample->valid_mask & AURORA_MEAS_VALID_BAT_I_EST) != 0U) &&
        (sample->battery_current_quality == AURORA_MEAS_QUALITY_ESTIMATED) &&
        (sample->battery_current_est_ma >= 0))
    {
        const int32_t error_ma =
            (int32_t)target_current_ma - sample->battery_current_est_ma;
        int64_t candidate_integral =
            ctx->cc_integral_mw +
            ((int64_t)error_ma * AURORA_CHARGER_CC_KI_MW_PER_MA_STEP);

        if (candidate_integral > (int64_t)AURORA_RATED_POWER_MW)
        {
            candidate_integral = AURORA_RATED_POWER_MW;
        }
        if (candidate_integral < -(int64_t)AURORA_RATED_POWER_MW)
        {
            candidate_integral = -(int64_t)AURORA_RATED_POWER_MW;
        }

        output_mw += (int64_t)error_ma * AURORA_CHARGER_CC_KP_MW_PER_MA;
        output_mw += candidate_integral;
        if (!(((output_mw >= (int64_t)AURORA_RATED_POWER_MW) && (error_ma > 0)) ||
              ((output_mw <= 0LL) && (error_ma < 0))))
        {
            ctx->cc_integral_mw = candidate_integral;
        }
    }
    else
    {
        /* 估算值无效时退回前馈，禁止沿用旧积分制造不可解释的功率请求。 */
        ctx->cc_integral_mw = 0LL;
    }

    return clamp_power(output_mw);
}

/*---------------------------------------------------------------------------*
 * Name        : static uint32_t voltage_power_target(aurora_charger_ctx_t *ctx,
 *               const aurora_measurement_t *sample, uint32_t target_mv)
 * Input       : ctx - 充电上下文；sample - 测量快照；target_mv - 电池电压目标
 * Output      : 电池侧目标功率，mW
 * Description : CV/Float共用电池电压PI，采用条件积分避免上下限饱和继续积累。
 *---------------------------------------------------------------------------*/
static uint32_t voltage_power_target(aurora_charger_ctx_t *ctx,
                                     const aurora_measurement_t *sample,
                                     uint32_t target_mv)
{
    const int32_t error_mv = (int32_t)target_mv - sample->battery_voltage_mv;
    const int64_t proportional_mw =
        (int64_t)error_mv * AURORA_CHARGER_CV_KP_MW_PER_MV;
    int64_t candidate_integral =
        ctx->cv_integral_mw +
        ((int64_t)error_mv * AURORA_CHARGER_CV_KI_MW_PER_MV_STEP);
    int64_t output_mw;

    if (candidate_integral < 0LL)
    {
        candidate_integral = 0LL;
    }
    if (candidate_integral > (int64_t)AURORA_RATED_POWER_MW)
    {
        candidate_integral = AURORA_RATED_POWER_MW;
    }

    output_mw = proportional_mw + candidate_integral;
    if (!(((output_mw >= (int64_t)AURORA_RATED_POWER_MW) && (error_mv > 0)) ||
          ((output_mw <= 0LL) && (error_mv < 0))))
    {
        ctx->cv_integral_mw = candidate_integral;
    }
    return clamp_power(proportional_mw + ctx->cv_integral_mw);
}

/*---------------------------------------------------------------------------*
 * Name        : bool aurora_charge_profile_get(aurora_battery_chem_t chemistry,
 *               aurora_battery_pack_t pack, aurora_charge_profile_t *out)
 * Input       : chemistry - 化学体系；pack - 48/60/72V档位；out - 输出地址
 * Output      : true表示档案有效；false表示索引或参数错误
 * Description : 从V2.7只读二维表取得档案，禁止越界索引进入充电和保护状态机。
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
 * Input       : ctx - 充电上下文；chemistry - 化学体系；pack - 电压档位；now_ms - 当前毫秒
 * Output      : 无
 * Description : 清零动态控制量并装载电池档案；档案无效时进入FAULT。
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
    ctx->initialized = aurora_charge_profile_get(chemistry, pack, &ctx->base_profile);
    ctx->profile = ctx->base_profile;
    ctx->state = ctx->initialized ? AURORA_CHARGE_OFF : AURORA_CHARGE_FAULT;
    ctx->state_since_ms = now_ms;
    ctx->charge_start_ms = now_ms;
}

/*---------------------------------------------------------------------------*
 * Name        : aurora_charge_output_t aurora_charger_step(
 *               aurora_charger_ctx_t *ctx, const aurora_measurement_t *sample,
 *               bool weak_light, bool thermal_limited, bool input_limited,
 *               uint32_t now_ms)
 * Input       : ctx - 充电上下文；sample - 测量；weak_light/thermal_limited/input_limited - 外部限制；now_ms - 当前毫秒
 * Output      : 充电阶段和电池侧目标
 * Description : 推进TC/CC/CV/Float/Complete；尾流只在真实控制余量充足时累计。
 *---------------------------------------------------------------------------*/
aurora_charge_output_t aurora_charger_step(aurora_charger_ctx_t *ctx,
                                           const aurora_measurement_t *sample,
                                           bool weak_light,
                                           bool thermal_limited,
                                           bool input_limited,
                                           uint32_t now_ms)
{
    aurora_charge_output_t output;
    const uint32_t required = AURORA_MEAS_VALID_BAT_V;

    memset(&output, 0, sizeof(output));
    if ((ctx == NULL) || (sample == NULL) || !ctx->initialized ||
        ((sample->valid_mask & required) != required) ||
        (sample->battery_voltage_mv <= 0))
    {
        output.state = AURORA_CHARGE_FAULT;
        return output;
    }

    if ((uint32_t)ctx->state > (uint32_t)AURORA_CHARGE_FAULT)
    {
        enter_state(ctx, AURORA_CHARGE_FAULT, now_ms);
    }

    update_effective_profile(ctx, sample);

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
        ctx->charge_start_ms = now_ms;
        ctx->cv_integral_mw = 0LL;
        ctx->cc_integral_mw = 0LL;
        enter_state(ctx,
                    ((uint32_t)sample->battery_voltage_mv < ctx->profile.trickle_exit_mv) ?
                        AURORA_CHARGE_TRICKLE : AURORA_CHARGE_CC,
                    now_ms);
        break;

    case AURORA_CHARGE_TRICKLE:
        if (condition_held(&ctx->transition_since_ms,
                           (uint32_t)sample->battery_voltage_mv >= ctx->profile.trickle_exit_mv,
                           AURORA_TRICKLE_TO_CC_HOLD_MS, now_ms))
        {
            ctx->cc_integral_mw = 0LL;
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_CC:
        if ((uint32_t)sample->battery_voltage_mv >= ctx->profile.cv_target_mv)
        {
            uint16_t increment = AURORA_CC_TO_CV_BASE_SCORE;
            if ((uint32_t)sample->battery_voltage_mv >=
                (ctx->profile.cv_target_mv + AURORA_CC_TO_CV_OVERDRIVE_1_MV))
            {
                increment = (uint16_t)(increment + AURORA_CC_TO_CV_OVERDRIVE_1_SCORE);
            }
            if ((uint32_t)sample->battery_voltage_mv >=
                (ctx->profile.cv_target_mv + AURORA_CC_TO_CV_OVERDRIVE_2_MV))
            {
                increment = (uint16_t)(increment + AURORA_CC_TO_CV_OVERDRIVE_2_SCORE);
            }
            ctx->cc_to_cv_score = (uint16_t)(ctx->cc_to_cv_score + increment);
            if (ctx->cc_to_cv_score > AURORA_CC_TO_CV_SCORE_THRESHOLD)
            {
                ctx->cv_integral_mw =
                    current_feedforward_mw((uint32_t)sample->battery_voltage_mv,
                                           ctx->profile.cc_current_ma);
                enter_state(ctx, AURORA_CHARGE_CV, now_ms);
            }
        }
        else if (((uint32_t)sample->battery_voltage_mv + AURORA_CC_TO_CV_OVERDRIVE_2_MV) <
                 ctx->profile.cv_target_mv)
        {
            ctx->cc_to_cv_score = 0U;
        }
        break;

    case AURORA_CHARGE_CV:
        if (condition_held(&ctx->transition_since_ms,
                           ((uint32_t)sample->battery_voltage_mv +
                            AURORA_CHARGER_CV_RETURN_HYST_MV) < ctx->profile.cv_target_mv,
                           AURORA_CV_TO_CC_HOLD_MS, now_ms))
        {
            ctx->cc_integral_mw = 0LL;
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        else if (((sample->valid_mask & AURORA_MEAS_VALID_BAT_I_EST) != 0U) &&
                 (sample->battery_current_quality == AURORA_MEAS_QUALITY_ESTIMATED) &&
                 !weak_light && !thermal_limited && !input_limited &&
                 ((uint32_t)sample->battery_voltage_mv >= ctx->profile.cv_min_mv) &&
                 ((uint32_t)sample->battery_voltage_mv <= ctx->profile.cv_max_mv) &&
                 (sample->battery_current_est_ma >= 0) &&
                 ((uint32_t)sample->battery_current_est_ma <= ctx->profile.tail_current_ma))
        {
            if (ctx->tail_since_ms == 0U)
            {
                ctx->tail_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->tail_since_ms) >= AURORA_TAIL_HOLD_MS)
            {
                if (ctx->profile.chemistry == AURORA_CHEM_LEAD)
                {
                    ctx->cv_integral_mw = 0LL;
                    enter_state(ctx, AURORA_CHARGE_FLOAT, now_ms);
                }
                else
                {
                    enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
                }
            }
        }
        else
        {
            ctx->tail_since_ms = 0U;
        }
        break;

    case AURORA_CHARGE_FLOAT:
        if (!ctx->float_started)
        {
            const uint32_t midpoint_mv =
                (ctx->profile.float_min_mv + ctx->profile.float_max_mv) / 2U;
            if (condition_held(&ctx->transition_since_ms,
                               (uint32_t)sample->battery_voltage_mv <= midpoint_mv,
                               AURORA_FLOAT_ENTRY_HOLD_MS, now_ms))
            {
                ctx->float_started = true;
                ctx->float_start_ms = now_ms;
                ctx->transition_since_ms = 0U;
                ctx->cv_integral_mw = 0LL;
            }
        }
        else if (elapsed_ms(now_ms, ctx->float_start_ms) >= AURORA_FLOAT_TIME_MS)
        {
            enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
        }
        else if (condition_held(&ctx->float_low_voltage_since_ms,
                                (uint32_t)sample->battery_voltage_mv < ctx->profile.float_min_mv,
                                AURORA_FLOAT_LOW_VOLT_HOLD_MS, now_ms))
        {
            ctx->charge_start_ms = now_ms;
            ctx->cc_integral_mw = 0LL;
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        else if (((sample->valid_mask & AURORA_MEAS_VALID_BAT_I_EST) != 0U) &&
                 !weak_light && !thermal_limited && !input_limited &&
                 ((uint32_t)sample->battery_voltage_mv >= ctx->profile.float_min_mv) &&
                 ((uint32_t)sample->battery_voltage_mv <= ctx->profile.float_max_mv) &&
                 (sample->battery_current_est_ma >= 0) &&
                 ((uint32_t)sample->battery_current_est_ma <=
                  ctx->profile.float_end_current_ma))
        {
            if (ctx->tail_since_ms == 0U)
            {
                ctx->tail_since_ms = now_ms;
            }
            else if (elapsed_ms(now_ms, ctx->tail_since_ms) >= AURORA_FLOAT_END_HOLD_MS)
            {
                enter_state(ctx, AURORA_CHARGE_COMPLETE, now_ms);
            }
        }
        else
        {
            ctx->tail_since_ms = 0U;
        }
        break;

    case AURORA_CHARGE_COMPLETE:
        if (condition_held(&ctx->recharge_since_ms,
                           (uint32_t)sample->battery_voltage_mv <= ctx->profile.recharge_mv,
                           AURORA_RECHARGE_HOLD_MS, now_ms))
        {
            ctx->charge_start_ms = now_ms;
            ctx->cc_integral_mw = 0LL;
            enter_state(ctx, AURORA_CHARGE_CC, now_ms);
        }
        break;

    case AURORA_CHARGE_FAULT:
        break;
    }

    output.state = ctx->state;
    output.weak_light = weak_light;
    output.input_limited = input_limited;
    output.thermal_limited = thermal_limited;
    output.voltage_target_mv = ctx->profile.cv_target_mv;

    switch (ctx->state)
    {
    case AURORA_CHARGE_TRICKLE:
        output.allow_charge = true;
        output.current_target_ma = ctx->profile.trickle_current_ma;
        output.battery_power_target_mw =
            current_power_target(ctx, sample, output.current_target_ma);
        break;

    case AURORA_CHARGE_CC:
        output.allow_charge = true;
        output.current_target_ma = ctx->profile.cc_current_ma;
        output.battery_power_target_mw =
            current_power_target(ctx, sample, output.current_target_ma);
        break;

    case AURORA_CHARGE_CV:
        output.allow_charge = true;
        output.voltage_target_mv = ctx->profile.cv_target_mv;
        output.battery_power_target_mw =
            voltage_power_target(ctx, sample, ctx->profile.cv_target_mv);
        break;

    case AURORA_CHARGE_FLOAT:
        output.allow_charge = ctx->float_started;
        output.voltage_target_mv = ctx->profile.float_target_mv;
        output.battery_power_target_mw = ctx->float_started ?
            voltage_power_target(ctx, sample, ctx->profile.float_target_mv) : 0U;
        break;

    case AURORA_CHARGE_OFF:
    case AURORA_CHARGE_COMPLETE:
    case AURORA_CHARGE_FAULT:
        output.allow_charge = false;
        output.battery_power_target_mw = 0U;
        break;
    }

    return output;
}
