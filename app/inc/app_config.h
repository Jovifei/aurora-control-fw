#ifndef AURORA_APP_CONFIG_H
#define AURORA_APP_CONFIG_H

#include <stdint.h>

/*
 * 应用层可调参数集中区。
 *
 * 规则：
 * 1. 所有量必须使用名称中声明的物理单位；
 * 2. 修改本文件前先阅读 docs/17-参数标定与Codex交接清单.md；
 * 3. 电池档案的逐化学体系参数位于 app/src/charger.c 的 k_profiles；
 * 4. 板级引脚、ADC比例、Flash地址和功率放行门禁位于 driver/inc/board_config.h。
 */

/* 功率BOM选择值：高功率版本。 */
#define AURORA_PROFILE_HIGH_POWER                   (1U)
/* 功率BOM选择值：低功率版本。 */
#define AURORA_PROFILE_LOW_POWER                    (2U)

/* 未由构建系统指定时，默认选择高功率BOM。 */
#ifndef AURORA_POWER_PROFILE
#define AURORA_POWER_PROFILE                        AURORA_PROFILE_HIGH_POWER
#endif

#if (AURORA_POWER_PROFILE != AURORA_PROFILE_HIGH_POWER) && \
    (AURORA_POWER_PROFILE != AURORA_PROFILE_LOW_POWER)
#error "AURORA_POWER_PROFILE must select a supported BOM profile"
#endif

#if AURORA_POWER_PROFILE == AURORA_PROFILE_HIGH_POWER
/* 高功率BOM的额定输入功率上限，单位mW。 */
#define AURORA_RATED_POWER_MW                       (300000U)
/* 高功率BOM的PV平均电流上限，单位mA。 */
#define AURORA_PV_CURRENT_LIMIT_MA                   (12000L)
/* 高功率BOM预充阶段的功率命令，单位mW。 */
#define AURORA_PRECHARGE_POWER_MW                    (15000U)
/* 高功率BOM预充阶段的目标电流参考，单位mA。 */
#define AURORA_PRECHARGE_CURRENT_MA                  (1000L)
/* 旧协议中上报的四字符产品型号。 */
#define AURORA_PRODUCT_MODEL                         "0300"
#else
/* 低功率BOM的额定输入功率上限，单位mW。 */
#define AURORA_RATED_POWER_MW                       (120000U)
/* 低功率BOM的PV平均电流上限，单位mA。 */
#define AURORA_PV_CURRENT_LIMIT_MA                   (7000L)
/* 低功率BOM预充阶段的功率命令，单位mW。 */
#define AURORA_PRECHARGE_POWER_MW                    (8000U)
/* 低功率BOM预充阶段的目标电流参考，单位mA。 */
#define AURORA_PRECHARGE_CURRENT_MA                  (600L)
/* 旧协议中上报的四字符产品型号。 */
#define AURORA_PRODUCT_MODEL                         "0120"
#endif

/* 应用层基础调度周期，单位ms。 */
#define AURORA_CONTROL_PERIOD_MS                     (10U)
/* 调试暂停后单次允许补计的最大时间，单位ms。 */
#define AURORA_MAX_ELAPSED_STEP_MS                   (1000U)
/* 1Wh对应的mW·ms，供整型能量累计使用。 */
#define AURORA_ONE_WH_MW_MS                          (3600000000ULL)
/* mV×mA换算为mW时使用的比例因子。 */
#define AURORA_MV_MA_PER_MW                          (1000ULL)

/* 低功率效率分段上边界，单位mW。 */
#define AURORA_EFFICIENCY_LOW_LIMIT_MW               (20000L)
/* 中功率效率分段上边界，单位mW。 */
#define AURORA_EFFICIENCY_MID_LIMIT_MW               (100000L)
/* 低功率段的保守效率估计，Q15，约85%。 */
#define AURORA_EFFICIENCY_LOW_Q15                    (27853U)
/* 中功率段的保守效率估计，Q15，约90%。 */
#define AURORA_EFFICIENCY_MID_Q15                    (29491U)
/* 高功率段的保守效率估计，Q15，约92%。 */
#define AURORA_EFFICIENCY_HIGH_Q15                   (30147U)
/* 允许电池电流估算的最低电池电压，单位mV。 */
#define AURORA_BATTERY_ESTIMATE_MIN_MV               (1000L)

/* PV进入启动/恢复流程的最低电压，单位mV。 */
#define AURORA_PV_START_MIN_MV                       (13000L)
/* PV软件绝对过压阈值，单位mV。 */
#define AURORA_PV_ABSOLUTE_MAX_MV                    (55000L)
/* 测量快照允许的最大数据年龄，单位ms。 */
#define AURORA_MEASUREMENT_STALE_MS                  (50U)
/* 上电后等待首个完整测量块的宽限时间，单位ms。 */
#define AURORA_MEASUREMENT_STARTUP_GRACE_MS          (250U)
/* 软件保护条件连续成立多少个控制样本后锁存。 */
#define AURORA_PROTECTION_DEBOUNCE_SAMPLES           (10U)
/* 低于该值视为电池尚未可靠接入，单位mV。 */
#define AURORA_BATTERY_CONNECTED_MIN_MV              (5000L)
/* 功率级允许进入预充的最低电池电压，单位mV。 */
#define AURORA_BATTERY_DETECT_MIN_MV                 (10000L)

/* MOS开始功率降额的温度，单位0.1°C。 */
#define AURORA_MOS_DERATE_TEMP_DC                    (750)
/* MOS软件过温跳闸温度，单位0.1°C。 */
#define AURORA_MOS_TRIP_TEMP_DC                      (900)
/* MOS软件过温恢复温度，单位0.1°C。 */
#define AURORA_MOS_RECOVER_TEMP_DC                   (800)
/* 允许的环境最低温度，单位0.1°C。 */
#define AURORA_AMB_MIN_TEMP_DC                       (-400)
/* 允许的环境最高温度，单位0.1°C。 */
#define AURORA_AMB_MAX_TEMP_DC                       (850)
/* 环境温度故障恢复迟滞，单位0.1°C。 */
#define AURORA_AMB_RECOVER_MARGIN_DC                 (50)

/* 预充时母线与电池压差允许吸合继电器的阈值，单位mV。 */
#define AURORA_RELAY_CLOSE_DELTA_MV                  (1500L)
/* 继电器吸合后母线与电池压差的复核阈值，单位mV。 */
#define AURORA_RELAY_VERIFY_DELTA_MV                 (2500L)
/* 压差连续满足吸合条件的保持时间，单位ms。 */
#define AURORA_RELAY_DELTA_HOLD_MS                   (1000U)
/* 继电器吸合后的机械稳定等待时间，单位ms。 */
#define AURORA_RELAY_SETTLE_MS                       (100U)
/* 故障关PWM后保留继电器的最短放能时间，单位ms。 */
#define AURORA_RELAY_FAULT_RELEASE_MS                (20U)
/* 预充未达到压差条件的最大允许时间，单位ms。 */
#define AURORA_PRECHARGE_TIMEOUT_MS                  (30000U)
/* 进入无光计时的PV功率阈值，单位mW。 */
#define AURORA_NO_SUN_ENTER_MW                       (1000L)
/* 从无光状态恢复的PV功率阈值，单位mW。 */
#define AURORA_NO_SUN_RECOVER_MW                     (3000L)
/* 连续无光后断开继电器的时间，单位ms。 */
#define AURORA_NO_SUN_OPEN_RELAY_MS                  (30UL * 60UL * 1000UL)

/* 物理Q6最小占空比，Q15，约2%。 */
#define AURORA_DUTY_MIN_Q15                          (655U)
/* 物理Q6最大占空比，Q15，约90%。 */
#define AURORA_DUTY_MAX_Q15                          (29491U)
/* 每次执行器允许变化的最大占空比步长，Q15。 */
#define AURORA_DUTY_STEP_Q15                         (256U)
/* 目标PWM频率，单位Hz；须与board计数配置一致。 */
#define AURORA_PWM_FREQUENCY_HZ                      (50000U)
/* 预充阶段最大占空比相对正常上限的除数。 */
#define AURORA_PRECHARGE_DUTY_LIMIT_DIVISOR          (2U)
/* 功率执行器积分增量的缩放除数。 */
#define AURORA_POWER_PI_INTEGRAL_DIVISOR             (128LL)
/* 功率执行器积分绝对限幅，单位Q15。 */
#define AURORA_POWER_PI_INTEGRAL_LIMIT_Q15           (4096LL)
/* 功率执行器比例项分子，单位Q15·mW/mW。 */
#define AURORA_POWER_PI_KP_NUMERATOR                 (64LL)
/* 功率执行器比例项分母，单位mW。 */
#define AURORA_POWER_PI_KP_DENOMINATOR_MW            (1000LL)

/* MPPT外层P-V搜索更新周期，单位ms。 */
#define AURORA_MPPT_UPDATE_MS                        (80U)
/* PV参考电压PI更新周期，单位ms。 */
#define AURORA_MPPT_PI_UPDATE_MS                     (10U)
/* MPPT忽略的最小电压变化，单位mV。 */
#define AURORA_MPPT_V_NOISE_MV                       (50L)
/* MPPT忽略的最小功率变化，单位mW。 */
#define AURORA_MPPT_P_NOISE_MW                       (500L)
/* MPPT最小参考电压步长，单位mV。 */
#define AURORA_MPPT_STEP_MIN_MV                      (50L)
/* MPPT最大参考电压步长，单位mV。 */
#define AURORA_MPPT_STEP_MAX_MV                      (500L)
/* 参考电压相对实时Voc必须保留的裕量，单位mV。 */
#define AURORA_MPPT_VOC_MARGIN_MV                    (500L)
/* MPPT参考下限相对PV启动门槛的裕量，单位mV。 */
#define AURORA_MPPT_REF_MIN_MARGIN_MV                (500L)
/* MPPT参考上限相对PV绝对上限的裕量，单位mV。 */
#define AURORA_MPPT_ABS_MAX_MARGIN_MV                (500L)
/* P-V斜率定点放大倍数。 */
#define AURORA_MPPT_SLOPE_SCALE                      (1000LL)
/* P-V斜率转换为自适应步长的除数。 */
#define AURORA_MPPT_SLOPE_STEP_DIVISOR               (20LL)
/* 快速下降阶段切换到正常跟踪的最小PV电流，单位mA。 */
#define AURORA_MPPT_FAST_DESCENT_CURRENT_MA          (200L)
/* PV参考电压PI比例增益，单位mW/mV。 */
#define AURORA_MPPT_VOLTAGE_KP_MW_PER_MV             (20LL)
/* PV参考电压PI每次更新的积分增益，单位mW/mV。 */
#define AURORA_MPPT_VOLTAGE_KI_MW_PER_MV_STEP        (1LL)

/* 充电CV环比例增益，单位mW/mV。 */
#define AURORA_CHARGER_CV_KP_MW_PER_MV               (50LL)
/* 充电CV环每次更新的积分增益，单位mW/mV。 */
#define AURORA_CHARGER_CV_KI_MW_PER_MV_STEP          (2LL)
/* CV电压低于目标多少时退回CC，单位mV。 */
#define AURORA_CHARGER_CV_RETURN_HYST_MV              (500U)
/* 单次充电允许的最长时间，单位ms。 */
#define AURORA_MAX_CHARGE_TIME_MS                    (900UL * 60UL * 1000UL)
/* 铅酸浮充允许的最长时间，单位ms。 */
#define AURORA_FLOAT_TIME_MS                         (180UL * 60UL * 1000UL)
/* 尾流连续满足判满条件的保持时间，单位ms。 */
#define AURORA_TAIL_HOLD_MS                          (1000U)

/* UI完整闪烁相位周期，单位ms。 */
#define AURORA_UI_PHASE_PERIOD_MS                    (2000U)
/* 故障灯一个闪烁周期，单位ms。 */
#define AURORA_UI_FAULT_BLINK_PERIOD_MS              (200U)
/* 故障灯每周期点亮时间，单位ms。 */
#define AURORA_UI_FAULT_ON_TIME_MS                   (100U)
/* 待机时RUN灯每相位周期点亮时间，单位ms。 */
#define AURORA_UI_STANDBY_PULSE_MS                   (200U)

/* 主动遥测帧发送周期，单位ms。 */
#define AURORA_TELEMETRY_PERIOD_MS                   (1000U)
/* 设置变更后等待合并写的时间，单位ms。 */
#define AURORA_STORAGE_DIRTY_HOLD_MS                 (1000U)
/* 固件主版本，供旧协议遥测字段使用。 */
#define AURORA_FW_VERSION_MAJOR                      (2U)
/* 固件次版本，供旧协议遥测字段使用。 */
#define AURORA_FW_VERSION_MINOR                      (0U)
/* 固件修订版本，供旧协议遥测字段使用。 */
#define AURORA_FW_VERSION_PATCH                      (0U)

/* 健康票据检查与喂狗窗口，单位ms。 */
#define AURORA_WATCHDOG_WINDOW_MS                    (100U)
/* 目标硬件IWDT名义超时，单位ms。 */
#define AURORA_WATCHDOG_TIMEOUT_MS                   (1000U)
/* 上电初始化阶段不要求完整健康票据的宽限时间，单位ms。 */
#define AURORA_WATCHDOG_STARTUP_GRACE_MS             (500U)

#endif
