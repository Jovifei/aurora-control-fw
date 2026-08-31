#ifndef AURORA_APP_TYPES_H
#define AURORA_APP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 应用层统一使用带单位的整数，避免把ADC码值、定时器CCR与物理量混用。
 * 电压：mV；电流：mA；功率：mW；温度：0.1°C；占空比：Q15。
 */

/* Q15中100%占空比对应的标度值。 */
#define AURORA_DUTY_Q15_ONE                         (32768U)
/* ADC逻辑通道总数，必须与板级扫描顺序一致。 */
#define AURORA_ADC_CHANNEL_COUNT                    (6U)
/* 每个DMA半缓冲包含的完整扫描次数。 */
#define AURORA_ADC_SCANS_PER_BLOCK                  (16U)
/* 单个DMA半缓冲包含的16位采样字数。 */
#define AURORA_ADC_BLOCK_WORDS                      (AURORA_ADC_CHANNEL_COUNT * \
                                                     AURORA_ADC_SCANS_PER_BLOCK)
/* 24h滚动能量保存49个累计Wh快照，等价于旧120W的48个30min间隔。 */
#define AURORA_ENERGY_HISTORY_POINT_COUNT           (49U)
/* 旧产品协议允许的最大数据载荷长度。 */
#define AURORA_PROTOCOL_MAX_DATA                    (127U)

/* 测量有效位：PV电压已由ADC直接测得。 */
#define AURORA_MEAS_VALID_PV_V                      (1UL << 0)
/* 测量有效位：PV电流已由ADC直接测得。 */
#define AURORA_MEAS_VALID_PV_I                      (1UL << 1)
/* 测量有效位：电池端电压已由ADC直接测得。 */
#define AURORA_MEAS_VALID_BAT_V                     (1UL << 2)
/* 测量有效位：继电器前母线电压已由ADC直接测得。 */
#define AURORA_MEAS_VALID_BUS_V                     (1UL << 3)
/* 测量有效位：MOS温度通道换算成功。 */
#define AURORA_MEAS_VALID_MOS_TEMP                  (1UL << 4)
/* 测量有效位：环境温度通道换算成功。 */
#define AURORA_MEAS_VALID_AMB_TEMP                  (1UL << 5)
/* 测量有效位：PV功率已由有效的V/I计算得到。 */
#define AURORA_MEAS_VALID_PV_POWER                  (1UL << 6)
/* 测量有效位：电池电流为功率换算估算值，不是独立ADC实测。 */
#define AURORA_MEAS_VALID_BAT_I_EST                 (1UL << 7)
/* 测量诊断位：BST_U原始码接近3.3V ADC满量程，当前分压下该电压不可用于Relay压差判断。 */
#define AURORA_MEAS_DIAG_BUS_ADC_SATURATED          (1UL << 0)

/* 故障位：MOS支路快速过流比较器触发。 */
#define AURORA_FAULT_FAST_MOS_OCP                   (1UL << 0)
/* 故障位：PV输入快速过流比较器触发。 */
#define AURORA_FAULT_FAST_PV_OCP                    (1UL << 1)
/* 故障位：PV软件欠压。 */
#define AURORA_FAULT_PV_UNDERVOLT                   (1UL << 2)
/* 故障位：PV软件过压。 */
#define AURORA_FAULT_PV_OVERVOLT                    (1UL << 3)
/* 故障位：电池端欠压。 */
#define AURORA_FAULT_BAT_UNDERVOLT                  (1UL << 4)
/* 故障位：任一级电池软件过压。 */
#define AURORA_FAULT_BAT_OVERVOLT                   (1UL << 5)
/* 故障位：继电器前母线过压。 */
#define AURORA_FAULT_BUS_OVERVOLT                   (1UL << 6)
/* 故障位：MOS温度超过软件跳闸阈值。 */
#define AURORA_FAULT_MOS_OVERTEMP                   (1UL << 7)
/* 故障位：环境温度超出允许范围。 */
#define AURORA_FAULT_AMB_TEMP                       (1UL << 8)
/* 故障位：ADC测量快照超时或尚未建立。 */
#define AURORA_FAULT_ADC_STALE                      (1UL << 9)
/* 故障位：预充/继电器/电池稳定验证失败。 */
#define AURORA_FAULT_RELAY                          (1UL << 10)
/* 故障位：电池档案或外部设置无效。 */
#define AURORA_FAULT_SETTINGS                       (1UL << 11)
/* 故障位：Flash Journal读写失败。 */
#define AURORA_FAULT_STORAGE                        (1UL << 12)
/* 故障位：无法归类的内部一致性错误。 */
#define AURORA_FAULT_INTERNAL                       (1UL << 13)
/* 故障位：ADC DMA硬件报告错误。 */
#define AURORA_FAULT_ADC_DMA                        (1UL << 14)
/* 故障位：DMA覆盖尚未处理的ADC半缓冲。 */
#define AURORA_FAULT_ADC_OVERRUN                    (1UL << 15)
/* 故障位：Break已锁存但比较器原因无法细分。 */
#define AURORA_FAULT_FAST_BREAK                     (1UL << 16)
/* 故障位：PV软件分级过流。 */
#define AURORA_FAULT_PV_OVERCURRENT                 (1UL << 17)
/* 故障位：PV持续过功率。 */
#define AURORA_FAULT_PV_OVERPOWER                   (1UL << 18)
/* 故障位：MOS NTC开路判据。 */
#define AURORA_FAULT_MOS_NTC_OPEN                   (1UL << 19)
/* 故障位：MOS NTC短路判据。 */
#define AURORA_FAULT_MOS_NTC_SHORT                  (1UL << 20)
/* 故障位：环境NTC开路判据。 */
#define AURORA_FAULT_AMB_NTC_OPEN                   (1UL << 21)
/* 故障位：环境NTC短路判据。 */
#define AURORA_FAULT_AMB_NTC_SHORT                  (1UL << 22)
/* 故障位：PV_I在PWM开/关状态下与物理行为不一致，提示零点/OPA/极性/采样链异常。 */
#define AURORA_FAULT_PV_CURRENT_PLAUSIBILITY       (1UL << 23)
/* 故障位：BST_U ADC进入近满量程，当前26:1分压下不允许据此闭合继电器。 */
#define AURORA_FAULT_BUS_ADC_SATURATION             (1UL << 24)
/* 故障位：Demo输出端存在外部电源、无持续负载或探测过载。 */
#define AURORA_FAULT_DEMO_OUTPUT                   (1UL << 25)

/* 通用函数返回状态。 */
typedef enum
{
    AURORA_STATUS_OK = 0,                            /* 操作成功。 */
    AURORA_STATUS_BUSY,                              /* 资源忙，稍后重试。 */
    AURORA_STATUS_INVALID,                           /* 参数或输入数据无效。 */
    AURORA_STATUS_NOT_READY,                         /* 前置条件尚未建立。 */
    AURORA_STATUS_IO_ERROR,                          /* 底层I/O失败。 */
    AURORA_STATUS_DENIED                             /* 安全策略拒绝操作。 */
} aurora_status_t;

/* 支持的电池化学体系。 */
typedef enum
{
    AURORA_CHEM_LEAD = 0,                            /* 铅酸。 */
    AURORA_CHEM_TERNARY,                             /* 三元锂。 */
    AURORA_CHEM_LFP,                                 /* 磷酸铁锂。 */
    AURORA_CHEM_SODIUM,                              /* 钠离子。 */
    AURORA_CHEM_COUNT                                /* 枚举数量，仅用于边界检查。 */
} aurora_battery_chem_t;

/* 支持的标称电池平台。 */
typedef enum
{
    AURORA_PACK_48V = 0,                             /* 48V平台。 */
    AURORA_PACK_60V,                                 /* 60V平台。 */
    AURORA_PACK_72V,                                 /* 72V平台。 */
    AURORA_PACK_COUNT                                /* 枚举数量，仅用于边界检查。 */
} aurora_battery_pack_t;

/* 产品运行模式与电池电压平台正交；Demo不能伪装成第四个电池档位。 */
typedef enum
{
    AURORA_MODE_BATTERY = 0,                         /* 正常电池充电模式。 */
    AURORA_MODE_DEMO_LOAD,                           /* 无电池、规定负载的受限演示输出。 */
    AURORA_MODE_COUNT                                /* 枚举数量，仅用于边界检查。 */
} aurora_operating_mode_t;

/* 测量值来源质量。 */
typedef enum
{
    AURORA_MEAS_QUALITY_INVALID = 0,                 /* 当前值不可用于控制。 */
    AURORA_MEAS_QUALITY_MEASURED,                    /* 来自独立硬件测量。 */
    AURORA_MEAS_QUALITY_ESTIMATED                    /* 由其他物理量和效率估算。 */
} aurora_measurement_quality_t;

/* NTC物理状态；使用uint8_t避免目标编译器枚举宽度影响测量快照布局。 */
typedef uint8_t aurora_ntc_status_t;
/* NTC采样正常，可参与温度计算与保护。 */
#define AURORA_NTC_STATUS_OK                        ((aurora_ntc_status_t)0U)
/* NTC下拉支路开路，ADC节点被5.1K上拉至接近VDD。 */
#define AURORA_NTC_STATUS_OPEN                      ((aurora_ntc_status_t)1U)
/* NTC或采样节点近似短接GND，ADC码接近0。 */
#define AURORA_NTC_STATUS_SHORT                     ((aurora_ntc_status_t)2U)

/* 经过滤波、标定和单位换算后的原子测量快照。 */
typedef struct
{
    uint32_t sequence;                               /* 每发布一次完整快照递增。 */
    uint32_t timestamp_ms;                           /* 该快照对应的采样时间。 */
    uint32_t valid_mask;                             /* AURORA_MEAS_VALID_*有效位。 */
    uint32_t diagnostic_mask;                        /* AURORA_MEAS_DIAG_*诊断位。 */
    int32_t pv_voltage_mv;                           /* PV输入电压，mV。 */
    int32_t pv_current_ma;                           /* PV输入电流，mA。 */
    int32_t pv_power_mw;                             /* PV输入功率，mW。 */
    int32_t battery_voltage_mv;                      /* 外部电池端电压，mV。 */
    int32_t bus_voltage_mv;                          /* 继电器前Boost母线电压，mV。 */
    int32_t battery_current_est_ma;                  /* 电池电流估算值，mA。 */
    aurora_measurement_quality_t battery_current_quality; /* 电池电流来源质量。 */
    uint8_t battery_current_quality_reserved[3];     /* 显式补齐质量枚举后的字节。 */
    uint16_t pv_current_raw;                         /* PV_I去极值平均后的ADC码。 */
    uint16_t bus_voltage_raw;                        /* BST_U去极值平均后的ADC码。 */
    uint16_t mos_ntc_raw;                            /* MOS NTC去极值平均ADC码。 */
    uint16_t ambient_ntc_raw;                        /* 环境NTC去极值平均ADC码。 */
    int16_t mos_temp_dC;                             /* MOS滤波温度，0.1°C。 */
    int16_t ambient_temp_dC;                         /* 环境滤波温度，0.1°C。 */
    aurora_ntc_status_t mos_ntc_status;              /* MOS NTC物理开/短/正常状态。 */
    aurora_ntc_status_t ambient_ntc_status;          /* 环境NTC物理开/短/正常状态。 */
    uint8_t ntc_status_reserved[2];                  /* 显式补齐。 */
} aurora_measurement_t;

/* 单个ADC逻辑通道的线性标定参数。 */
typedef struct
{
    int32_t gain_num;                                /* 增益分子，输出物理单位/码。 */
    int32_t gain_den;                                /* 增益分母，不得为0。 */
    int32_t offset;                                  /* 换算后的固定物理量偏移。 */
    int16_t zero_code;                               /* 双向电流通道的零电流码值。 */
    int8_t polarity;                                 /* +1正向，-1反向。 */
    bool valid;                                      /* 完成标定且允许参与控制。 */
} aurora_adc_calibration_t;

/* 全部ADC逻辑通道的标定集合。 */
typedef struct
{
    aurora_adc_calibration_t channel[AURORA_ADC_CHANNEL_COUNT]; /* 按扫描顺序排列。 */
} aurora_measurement_calibration_t;

/* 电池充电阶段。 */
typedef enum
{
    AURORA_CHARGE_OFF = 0,                           /* 未充电。 */
    AURORA_CHARGE_TRICKLE,                           /* 低压预充/涓流。 */
    AURORA_CHARGE_CC,                                /* 恒流主充。 */
    AURORA_CHARGE_CV,                                /* 恒压吸收。 */
    AURORA_CHARGE_FLOAT,                             /* 铅酸浮充。 */
    AURORA_CHARGE_COMPLETE,                          /* 已满足充满判据。 */
    AURORA_CHARGE_FAULT                              /* 充电状态机故障停止。 */
} aurora_charge_state_t;

/* 功率级启动、连接与发波状态。 */
typedef enum
{
    AURORA_POWER_OFF = 0,                            /* PWM与继电器均关闭。 */
    AURORA_POWER_WAIT_PV,                            /* 等待PV进入启动窗口。 */
    AURORA_POWER_START_DELAY,                        /* V2.7动态/固定启动延时。 */
    AURORA_POWER_ZERO_CAL,                           /* PWM关闭状态下校准PV_I零点。 */
    AURORA_POWER_WAIT_BATTERY,                       /* Battery模式等待有效电池端电压。 */
    AURORA_POWER_PRECHARGE,                          /* Relay断开，受限Boost建立BST_U均压。 */
    AURORA_POWER_RELAY_SETTLE,                       /* Battery模式Relay吸合并机械稳定。 */
    AURORA_POWER_BAT_STABILITY,                      /* Relay闭合、PWM关闭，验证BAT_U 10s稳定性。 */
    AURORA_POWER_RUN,                                /* 正常MPPT电池充电。 */
    AURORA_POWER_DEMO_OUTPUT_CHECK,                  /* Demo模式确认输出端无外部有源电压。 */
    AURORA_POWER_DEMO_RELAY_SETTLE,                  /* Demo模式在PWM关闭时连接规定负载。 */
    AURORA_POWER_DEMO_PROBE,                         /* Demo模式低功率主动探测持续负载。 */
    AURORA_POWER_DEMO_RUN,                           /* Demo模式受限CV/功率输出。 */
    AURORA_POWER_NO_SUN,                             /* 真正无PV后断开继电器。 */
    AURORA_POWER_FAULT                               /* 故障关断与放能。 */
} aurora_power_state_t;

/* 启动失败内部分类；外部仍映射到既有故障位和三组LED。 */
typedef uint8_t aurora_start_failure_reason_t;
#define AURORA_START_FAIL_NONE                     ((aurora_start_failure_reason_t)0U) // 无启动失败。
#define AURORA_START_FAIL_ZERO_CAL                 ((aurora_start_failure_reason_t)1U) // PV_I零点校准连续失败。
#define AURORA_START_FAIL_PV_WEAK                  ((aurora_start_failure_reason_t)2U) // PV输入功率不足，属于可等待条件。
#define AURORA_START_FAIL_BUS_PRECHARGE_TIMEOUT    ((aurora_start_failure_reason_t)3U) // BST_U在限定时间内未完成均压。
#define AURORA_START_FAIL_BUS_OVERSHOOT            ((aurora_start_failure_reason_t)4U) // BST_U相对BAT_U过冲或绝对过压。
#define AURORA_START_FAIL_BUS_MEAS_INVALID         ((aurora_start_failure_reason_t)5U) // BST_U测量无效或接近ADC满量程。
#define AURORA_START_FAIL_RELAY_CLOSE_VERIFY       ((aurora_start_failure_reason_t)6U) // Relay闭合后二次压差验证失败。
#define AURORA_START_FAIL_BAT_STABILITY            ((aurora_start_failure_reason_t)7U) // Relay闭合后BAT_U稳定窗口失败。
#define AURORA_START_FAIL_DEMO_EXTERNAL_SOURCE     ((aurora_start_failure_reason_t)8U) // Demo启动前检测到输出端已有外部电压。
#define AURORA_START_FAIL_DEMO_NO_LOAD             ((aurora_start_failure_reason_t)9U) // Demo探测未获得持续负载证据。
#define AURORA_START_FAIL_DEMO_OVERLOAD            ((aurora_start_failure_reason_t)10U) // Demo探测或运行出现短路/过载。

/* 由化学体系和电压平台选择的充电及电池软件保护档案。 */
typedef struct
{
    uint32_t battery_uv_mv;                          /* 电池欠压保护，mV。 */
    uint32_t battery_uv_recover_mv;                  /* 电池欠压恢复，mV。 */
    uint32_t trickle_exit_mv;                        /* 涓流转恒流阈值，mV。 */
    uint32_t cv_target_mv;                           /* 恒压控制目标，mV。 */
    uint32_t cv_min_mv;                              /* 恒压验收下限，mV。 */
    uint32_t cv_max_mv;                              /* 恒压验收上限，mV。 */
    uint32_t ov_slow_mv;                             /* 电池软件一级过压阈值，mV。 */
    uint32_t ov_medium_mv;                           /* 一级阈值+0.7V的1s过压阈值，mV。 */
    uint32_t ov_fast_mv;                             /* 3ms快速软件过压阈值，mV。 */
    uint32_t ov_absolute_mv;                         /* 统一绝对软件过压阈值，mV。 */
    uint32_t float_target_mv;                        /* 铅酸浮充目标，mV；其他体系为0。 */
    uint32_t float_min_mv;                           /* 铅酸浮充验收下限，mV。 */
    uint32_t float_max_mv;                           /* 铅酸浮充验收上限，mV。 */
    uint32_t full_voltage_mv;                        /* 产品表定义的满充电压，mV。 */
    uint32_t recharge_mv;                            /* 充满后重新允许充电的电压，mV。 */
    uint32_t trickle_current_ma;                     /* 涓流阶段电流上限，mA。 */
    uint32_t cc_current_ma;                          /* 恒流阶段电流目标，mA。 */
    uint32_t tail_current_ma;                        /* CV判满尾流阈值，mA。 */
    uint32_t float_end_current_ma;                   /* 铅酸浮充结束电流，mA。 */
    int16_t ambient_low_trip_dC;                     /* 当前化学体系低温停充点，0.1°C。 */
    int16_t ambient_low_recover_dC;                  /* 当前化学体系低温恢复点，0.1°C。 */
    aurora_battery_chem_t chemistry;                 /* 化学体系。 */
    aurora_battery_pack_t pack;                      /* 标称电压平台。 */
    uint8_t layout_reserved[2];                      /* 显式补齐ARM短枚举后的结构尾部。 */
} aurora_charge_profile_t;

/* 充电状态机给上层的“电池侧目标”，不直接冒充PV输入功率。 */
typedef struct
{
    uint32_t battery_power_target_mw;                /* 当前电池侧目标功率，mW。 */
    uint32_t pv_power_limit_mw;                      /* 经估算/包络换算后的PV侧允许功率，mW。 */
    uint32_t current_target_ma;                      /* 当前电池侧目标电流，mA。 */
    uint32_t voltage_target_mv;                      /* 当前电池侧目标电压，mV。 */
    aurora_charge_state_t state;                     /* 当前充电阶段。 */
    bool allow_charge;                               /* true表示当前阶段允许能量传输。 */
    bool weak_light;                                 /* true表示PV功率不足。 */
    bool input_limited;                              /* true表示PV电流/电压/功率包络正在限幅。 */
    bool thermal_limited;                            /* true表示温度包络正在限幅。 */
    bool restart_required;                           /* true表示本轮阶段转换必须先退出RUN并重新做电池稳定准入。 */
    uint8_t output_reserved[2];                      /* 显式补齐布尔字段。 */
} aurora_charge_output_t;

/* MPPT外环和PV电压PI输出。 */
typedef struct
{
    uint32_t target_voltage_mv;                      /* PV参考电压，mV。 */
    uint32_t theoretical_power_mw;                   /* 未经电池/硬件裁决的功率请求，mW。 */
    bool valid;                                      /* true表示当前样本足以形成控制请求。 */
    uint8_t valid_reserved[3];                       /* 显式补齐有效位后的字节。 */
} aurora_mppt_output_t;

/* 应用层提交给Service的硬件无关功率命令。 */
typedef struct
{
    uint16_t duty_q15;                               /* 低侧MOS物理占空比，Q15。 */
    aurora_power_state_t state;                      /* 命令对应的功率级状态。 */
    bool pwm_enable;                                 /* true表示请求发波。 */
    bool relay_enable;                               /* true表示请求闭合继电器。 */
    uint8_t state_reserved[3];                       /* 显式补齐功率状态字段。 */
} aurora_power_command_t;

/* LED逻辑输出。 */
typedef struct
{
    bool led_run_on;                                 /* RUN灯逻辑点亮请求。 */
    bool led_fault_on;                               /* FAULT灯逻辑点亮请求。 */
} aurora_ui_output_t;

/* 需要持久化的用户设置、双能量账本、Demo参数和24h滚动窗口。 */
typedef struct
{
    uint64_t pv_energy_remainder_mw_ms;              /* PV发电量不足1Wh的余数，mW·ms。 */
    uint64_t charge_est_energy_remainder_mw_ms;      /* 电池侧估算充电量不足1Wh余数，mW·ms。 */
    uint32_t lifetime_energy_wh;                     /* PV实测生命周期发电量，Wh；保留旧字段名兼容协议。 */
    uint32_t daily_energy_wh;                        /* PV实测最近24h发电量，Wh。 */
    uint32_t charge_est_lifetime_energy_wh;          /* 电池侧估算生命周期充电量，Wh。 */
    uint32_t charge_est_daily_energy_wh;             /* 电池侧估算最近24h充电量，Wh。 */
    uint32_t settings_revision;                      /* 每次有效设置变更递增。 */
    uint32_t history_interval_elapsed_ms;            /* 当前30min窗口已累计时间，ms。 */
    uint32_t demo_target_voltage_mv;                 /* Demo输出目标电压，mV。 */
    uint32_t demo_power_limit_mw;                    /* Demo输入功率上限，mW。 */
    uint32_t energy_history_wh[AURORA_ENERGY_HISTORY_POINT_COUNT]; /* PV累计Wh历史快照。 */
    uint32_t charge_est_history_wh[AURORA_ENERGY_HISTORY_POINT_COUNT]; /* 电池侧估算累计Wh历史。 */
    aurora_battery_chem_t chemistry;                 /* 用户选择的电池化学体系。 */
    aurora_battery_pack_t pack;                      /* 用户选择的电压平台。 */
    aurora_operating_mode_t operating_mode;          /* Battery或受限Demo模式。 */
    uint8_t energy_history_count;                    /* 两套历史共用的有效点数，1~49。 */
    uint8_t energy_semantics_version;                /* 能量字段物理语义版本。 */
    uint8_t layout_reserved[3];                      /* 显式补齐到uint64_t对齐边界。 */
} aurora_persistent_settings_t;

#ifdef __cplusplus
}
#endif

#endif
