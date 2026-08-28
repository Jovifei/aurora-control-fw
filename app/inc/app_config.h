#ifndef AURORA_APP_CONFIG_H
#define AURORA_APP_CONFIG_H

#include <stdint.h>

/*
 * 应用层可调参数集中区。
 *
 * 参数来源优先级：
 * 1. 120W V2.7 CheckList：成熟产品行为、充电参数、保护时间；
 * 2. 300W最终原理图：硬件能力和采样/驱动路径；
 * 3. 300W台架：功率、电流、温升和SOA相关参数的最终冻结值。
 *
 * 带“候选”说明的300W功率参数不得视为已经台架验证。
 */

/* 功率BOM选择值：默认高功率版本。 */
#define AURORA_PROFILE_HIGH_POWER                   (1U)
/* 功率BOM选择值：兼容低功率版本。 */
#define AURORA_PROFILE_LOW_POWER                    (2U)

#ifndef AURORA_POWER_PROFILE
#define AURORA_POWER_PROFILE                        AURORA_PROFILE_HIGH_POWER
#endif

#if (AURORA_POWER_PROFILE != AURORA_PROFILE_HIGH_POWER) && \
    (AURORA_POWER_PROFILE != AURORA_PROFILE_LOW_POWER)
#error "AURORA_POWER_PROFILE must select a supported BOM profile"
#endif

#if AURORA_POWER_PROFILE == AURORA_PROFILE_HIGH_POWER
/* 300W BOM额定输入功率，mW。 */
#define AURORA_RATED_POWER_MW                       (300000U)
/* 300W PV平均电流候选上限，mA；必须经电感/MOS/二极管/PCB和台架确认。 */
#define AURORA_PV_CURRENT_LIMIT_MA                  (12000L)
/* 继电器断开时用于建立BST_U的受限预充功率，mW。 */
#define AURORA_PRECHARGE_POWER_MW                   (15000U)
/* 95~104°C降额末端保留的最小输入功率，mW；300W台架前仅为保守候选。 */
#define AURORA_THERMAL_MIN_POWER_MW                 (9000U)
/* 旧协议四字符型号。 */
#define AURORA_PRODUCT_MODEL                        "0300"
#else
/* 120W兼容BOM额定输入功率，mW。 */
#define AURORA_RATED_POWER_MW                       (120000U)
/* V2.7成熟基础PV限流值，mA。 */
#define AURORA_PV_CURRENT_LIMIT_MA                  (8000L)
/* 低功率BOM预充功率，mW。 */
#define AURORA_PRECHARGE_POWER_MW                   (8000U)
/* V2.7温度降额末端约7~9W，取9W。 */
#define AURORA_THERMAL_MIN_POWER_MW                 (9000U)
/* 旧协议四字符型号。 */
#define AURORA_PRODUCT_MODEL                        "0120"
#endif

/* 应用层基础调度周期，ms。保护延时使用真实时间戳，不再使用统一样本计数。 */
#define AURORA_CONTROL_PERIOD_MS                    (10U)
/* 调试暂停后单次允许补计的最大时间，ms。 */
#define AURORA_MAX_ELAPSED_STEP_MS                  (1000U)
/* 1Wh对应的mW·ms。 */
#define AURORA_ONE_WH_MW_MS                         (3600000000ULL)
/* mV×mA换算为mW的比例。 */
#define AURORA_MV_MA_PER_MW                         (1000ULL)

/* 无BAT_I硬件时的保守效率分段。 */
#define AURORA_EFFICIENCY_LOW_LIMIT_MW              (20000L)
#define AURORA_EFFICIENCY_MID_LIMIT_MW              (100000L)
#define AURORA_EFFICIENCY_LOW_Q15                   (27853U)  /* 约85%。 */
#define AURORA_EFFICIENCY_MID_Q15                   (29491U)  /* 约90%。 */
#define AURORA_EFFICIENCY_HIGH_Q15                  (30147U)  /* 约92%。 */
#define AURORA_BATTERY_ESTIMATE_MIN_MV              (1000L)

/* ---------------- PV电压、启动与真正无发电 ---------------- */
/* V2.7 PV欠压：<8V持续1s保护，>9V持续1s恢复。 */
#define AURORA_PV_UV_TRIP_MV                        (8000L)
#define AURORA_PV_UV_RECOVER_MV                     (9000L)
#define AURORA_PV_UV_TRIP_DELAY_MS                  (1000U)
#define AURORA_PV_UV_RECOVER_DELAY_MS               (1000U)
/* V2.7 PV过压：>55V持续1s保护，<54V持续1s恢复。 */
#define AURORA_PV_OV_TRIP_MV                        (55000L)
#define AURORA_PV_OV_RECOVER_MV                     (54000L)
#define AURORA_PV_OV_TRIP_DELAY_MS                  (1000U)
#define AURORA_PV_OV_RECOVER_DELAY_MS               (1000U)
/* MPPT绝对参考上界仍以55V为软件边界。 */
#define AURORA_PV_ABSOLUTE_MAX_MV                   AURORA_PV_OV_TRIP_MV

/* V1.12.19启动窗口：>15V动态1~10s；13~15V固定15s；<13V不启动。 */
#define AURORA_PV_START_MIN_MV                      (13000L)
#define AURORA_PV_FAST_START_MV                     (15000L)
#define AURORA_START_DELAY_MIN_MS                   (1000U)
#define AURORA_START_DELAY_MAX_MS                   (10000U)
#define AURORA_START_DELAY_STEP_MS                  (1000U)
#define AURORA_START_MID_VOLTAGE_DELAY_MS           (15000U)
/* 正常充电估算电流>=80mA持续1s，下一次启动延时减少1s。 */
#define AURORA_START_SUCCESS_CURRENT_MA             (80L)
#define AURORA_START_SUCCESS_HOLD_MS                (1000U)
/* 真正无PV：PV<13V且MPPT/PWM未运行连续30min，才断开继电器/Link。 */
#define AURORA_NO_SUN_OPEN_RELAY_MS                 (30UL * 60UL * 1000UL)

/* ---------------- PV_I上电零点校准 ---------------- */
/* PV>13V稳定2s后，在PWM和继电器关闭状态下开始PV_I零点校准。 */
#define AURORA_ZERO_CAL_PV_STABLE_MS                (2000U)
/* 使用32个完整DMA块平均得到运行时zero_code。 */
#define AURORA_ZERO_CAL_BLOCKS                      (32U)
/* 零点码安全窗口是工程候选范围，越界不允许发波。 */
#define AURORA_ZERO_CAL_CODE_MIN                    (1024U)
#define AURORA_ZERO_CAL_CODE_MAX                    (3072U)

/* ---------------- 测量时效 ---------------- */
#define AURORA_MEASUREMENT_STALE_MS                 (50U)
#define AURORA_MEASUREMENT_STARTUP_GRACE_MS         (250U)
#define AURORA_BATTERY_CONNECTED_MIN_MV             (5000L)
#define AURORA_BATTERY_DETECT_MIN_MV                (10000L)

/* ---------------- 继电器预充：硬性安全顺序 ---------------- */
/* BST_U必须先由Boost充到与BAT_U压差<=1.5V，并连续稳定1s，才允许吸合继电器。 */
#define AURORA_RELAY_CLOSE_DELTA_MV                 (1500L)
#define AURORA_RELAY_DELTA_HOLD_MS                  (1000U)
/* 吸合后机械稳定100ms，再复核压差不得超过2.5V。 */
#define AURORA_RELAY_SETTLE_MS                      (100U)
#define AURORA_RELAY_VERIFY_DELTA_MV                (2500L)
/* 继电器闭合、PWM保持关闭，再观察BAT_U完整10s，max-min<=2V才进入RUN。 */
#define AURORA_BAT_STABILITY_WINDOW_MS              (10000U)
#define AURORA_BAT_STABILITY_MAX_SPAN_MV            (2000L)
/* 预充超过30s仍无法满足压差条件，判本次启动失败。 */
#define AURORA_PRECHARGE_TIMEOUT_MS                 (30000U)
/* 故障关PWM后至少保持20ms放能，再释放继电器。 */
#define AURORA_RELAY_FAULT_RELEASE_MS               (20U)

/* ---------------- PV电流与过功率 ---------------- */
/* V2.7分级OCP比例：1.2倍/10s，1.35倍/1s，1.5倍/100ms。 */
#define AURORA_PV_OCP_SLOW_NUM                      (120L)
#define AURORA_PV_OCP_MID_NUM                       (135L)
#define AURORA_PV_OCP_FAST_NUM                      (150L)
#define AURORA_PV_OCP_RATIO_DEN                     (100L)
#define AURORA_PV_OCP_SLOW_DELAY_MS                 (10000U)
#define AURORA_PV_OCP_MID_DELAY_MS                  (1000U)
#define AURORA_PV_OCP_FAST_DELAY_MS                 (100U)
/* 软件过流恢复到基础限流以下并稳定30s后允许重新启动。 */
#define AURORA_PV_OCP_RECOVER_DELAY_MS              (30000U)
/* 持续过功率候选策略：额定功率×1.2持续5s。 */
#define AURORA_OVERPOWER_NUM                        (120ULL)
#define AURORA_OVERPOWER_DEN                        (100ULL)
#define AURORA_OVERPOWER_DELAY_MS                   (5000U)
#define AURORA_OVERPOWER_RECOVER_DELAY_MS           (30000U)

/* V2.7低输入电压功率包络：<=12V为50W；>=17V为当前BOM额定功率；中间线性。 */
#define AURORA_LOW_PV_POWER_START_MV                (12000L)
#define AURORA_LOW_PV_POWER_FULL_MV                 (17000L)
#define AURORA_LOW_PV_POWER_FLOOR_MW                (50000U)

/* ---------------- 电池软件保护时间 ---------------- */
#define AURORA_BAT_UV_TRIP_DELAY_MS                 (1000U)
#define AURORA_BAT_UV_RECOVER_DELAY_MS              (1000U)
/* 一级OV：档案阈值5s；阈值+0.7V为1s；快速阈值3ms；绝对93V为1s。 */
#define AURORA_BAT_OV_SLOW_DELAY_MS                 (5000U)
#define AURORA_BAT_OV_MEDIUM_DELAY_MS               (1000U)
#define AURORA_BAT_OV_FAST_DELAY_MS                 (3U)
#define AURORA_BAT_OV_ABSOLUTE_DELAY_MS             (1000U)
/* V2.7快速OV恢复栏存在跨平台歧义；软件保守采用回到CV上限以下并稳定2.5s。 */
#define AURORA_BAT_OV_RECOVER_DELAY_MS              (2500U)

/* ---------------- 温度与NTC ---------------- */
/* 用户确认：300W/本版本MOS从95°C开始降额，不再使用90°C。 */
#define AURORA_MOS_DERATE_TEMP_DC                   (950)
#define AURORA_MOS_DERATE_END_TEMP_DC               (1040)
/* V2.7 MOS 105°C/1s停机，降到95°C/1s恢复。 */
#define AURORA_MOS_TRIP_TEMP_DC                     (1050)
#define AURORA_MOS_RECOVER_TEMP_DC                  (950)
#define AURORA_MOS_TEMP_TRIP_DELAY_MS               (1000U)
#define AURORA_MOS_TEMP_RECOVER_DELAY_MS            (1000U)
/* V2.7环境高温55/50°C、低温-20/-15°C，均1s确认。 */
#define AURORA_AMB_HIGH_TRIP_TEMP_DC                (550)
#define AURORA_AMB_HIGH_RECOVER_TEMP_DC             (500)
#define AURORA_AMB_LOW_TRIP_TEMP_DC                 (-200)
#define AURORA_AMB_LOW_RECOVER_TEMP_DC              (-150)
#define AURORA_AMB_TEMP_TRIP_DELAY_MS               (1000U)
#define AURORA_AMB_TEMP_RECOVER_DELAY_MS            (1000U)
/* V2.7 NTC边界：>=125°C按开路，<=-40°C按短路；恢复也需稳定1s。 */
#define AURORA_NTC_OPEN_TEMP_DC                     (1250)
#define AURORA_NTC_SHORT_TEMP_DC                    (-400)
#define AURORA_NTC_FAULT_DELAY_MS                   (1000U)
#define AURORA_NTC_RECOVER_DELAY_MS                 (1000U)

/* ---------------- Duty/PWM ---------------- */
#define AURORA_DUTY_MIN_Q15                         (655U)    /* 约2%。 */
#define AURORA_DUTY_MAX_Q15                         (29491U)  /* 约90%。 */
#define AURORA_DUTY_STEP_Q15                        (256U)
#define AURORA_PWM_FREQUENCY_HZ                     (50000U)
#define AURORA_PRECHARGE_DUTY_LIMIT_DIVISOR         (2U)
#define AURORA_POWER_PI_INTEGRAL_DIVISOR            (128LL)
#define AURORA_POWER_PI_INTEGRAL_LIMIT_Q15          (4096LL)
#define AURORA_POWER_PI_KP_NUMERATOR                (64LL)
#define AURORA_POWER_PI_KP_DENOMINATOR_MW           (1000LL)

/* ---------------- MPPT ---------------- */
#define AURORA_MPPT_UPDATE_MS                       (80U)
#define AURORA_MPPT_PI_UPDATE_MS                    (10U)
#define AURORA_MPPT_V_NOISE_MV                      (50L)
#define AURORA_MPPT_P_NOISE_MW                      (500L)
#define AURORA_MPPT_STEP_MIN_MV                     (50L)
#define AURORA_MPPT_STEP_MAX_MV                     (500L)
#define AURORA_MPPT_VOC_MARGIN_MV                   (500L)
#define AURORA_MPPT_REF_MIN_MARGIN_MV               (500L)
#define AURORA_MPPT_ABS_MAX_MARGIN_MV               (500L)
#define AURORA_MPPT_SLOPE_SCALE                     (1000LL)
#define AURORA_MPPT_SLOPE_STEP_DIVISOR              (20LL)
#define AURORA_MPPT_FAST_DESCENT_CURRENT_MA         (200L)
#define AURORA_MPPT_VOLTAGE_KP_MW_PER_MV            (20LL)
#define AURORA_MPPT_VOLTAGE_KI_MW_PER_MV_STEP       (1LL)

/* ---------------- 充电控制 ---------------- */
#define AURORA_CHARGER_CV_KP_MW_PER_MV              (50LL)
#define AURORA_CHARGER_CV_KI_MW_PER_MV_STEP         (2LL)
#define AURORA_CHARGER_CV_RETURN_HYST_MV            (500U)
/* 无BAT_I硬件：CC以电池功率前馈+BAT_I_EST低带宽PI修正，增益为候选值。 */
#define AURORA_CHARGER_CC_KP_MW_PER_MA              (10LL)
#define AURORA_CHARGER_CC_KI_MW_PER_MA_STEP         (1LL)
#define AURORA_MAX_CHARGE_TIME_MS                   (900UL * 60UL * 1000UL)
#define AURORA_FLOAT_TIME_MS                        (180UL * 60UL * 1000UL)
#define AURORA_TAIL_HOLD_MS                         (1000U)

/* ---------------- CMP快速故障恢复 ---------------- */
/* PWM真正运行后的快速OCP锁存，源消失并保持30s后才允许清除并重新走启动。 */
#define AURORA_FAST_OCP_RECOVER_DELAY_MS            (30000U)

/* ---------------- LED ---------------- */
/* RUN充电时500ms翻转；非充电且系统工作时常亮。 */
#define AURORA_UI_RUN_BLINK_HALF_MS                 (500U)
/* FAULT按500ms亮/灭输出1/2/3次闪烁，然后保留2s组间隔。 */
#define AURORA_UI_FAULT_HALF_MS                     (500U)
#define AURORA_UI_FAULT_GROUP_GAP_MS                (2000U)

/* 主动遥测与存储。 */
#define AURORA_TELEMETRY_PERIOD_MS                  (1000U)
#define AURORA_STORAGE_DIRTY_HOLD_MS                (1000U)
#define AURORA_FW_VERSION_MAJOR                     (2U)
#define AURORA_FW_VERSION_MINOR                     (0U)
#define AURORA_FW_VERSION_PATCH                     (8U)

/* 看门狗健康监督。 */
#define AURORA_WATCHDOG_WINDOW_MS                   (100U)
#define AURORA_WATCHDOG_TIMEOUT_MS                  (1000U)
#define AURORA_WATCHDOG_STARTUP_GRACE_MS            (500U)

#endif
