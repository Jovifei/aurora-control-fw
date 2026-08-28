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
/* 旧产品协议允许的最大数据载荷长度。 */
#define AURORA_PROTOCOL_MAX_DATA                    (127U)

/* ADC标定类型：线性通道使用零点/增益，NTC通道使用Beta曲线。 */
#define AURORA_ADC_CALIBRATION_LINEAR               (0U)
/* ADC标定类型：由分压电阻和Beta参数换算NTC温度。 */
#define AURORA_ADC_CALIBRATION_NTC_BETA             (1U)

/* 测量有效位：PV电压已由ADC直接测得。 */
#define AURORA_MEAS_VALID_PV_V                      (1UL << 0)
/* 测量有效位：PV电流已由ADC直接测得。 */
#define AURORA_MEAS_VALID_PV_I                      (1UL << 1)
/* 测量有效位：电池端电压已由ADC直接测得。 */
#define AURORA_MEAS_VALID_BAT_V                     (1UL << 2)
/* 测量有效位：继电器前母线电压已由ADC直接测得。 */
#define AURORA_MEAS_VALID_BUS_V                     (1UL << 3)
/* 测量有效位：MOS温度通道已完成标定并可用。 */
#define AURORA_MEAS_VALID_MOS_TEMP                  (1UL << 4)
/* 测量有效位：环境温度通道已完成标定并可用。 */
#define AURORA_MEAS_VALID_AMB_TEMP                  (1UL << 5)
/* 测量有效位：PV功率已由有效的V/I计算得到。 */
#define AURORA_MEAS_VALID_PV_POWER                  (1UL << 6)
/* 测量有效位：电池电流为功率换算估算值，不是独立ADC实测。 */
#define AURORA_MEAS_VALID_BAT_I_EST                 (1UL << 7)

/* 故障位：MOS支路快速过流比较器触发。 */
#define AURORA_FAULT_FAST_MOS_OCP                   (1UL << 0)
/* 故障位：PV输入快速过流比较器触发。 */
#define AURORA_FAULT_FAST_PV_OCP                    (1UL << 1)
/* 故障位：PV欠压持续超过软件去抖时间。 */
#define AURORA_FAULT_PV_UNDERVOLT                   (1UL << 2)
/* 故障位：PV过压持续超过软件去抖时间。 */
#define AURORA_FAULT_PV_OVERVOLT                    (1UL << 3)
/* 故障位：电池端欠压。 */
#define AURORA_FAULT_BAT_UNDERVOLT                  (1UL << 4)
/* 故障位：电池端过压。 */
#define AURORA_FAULT_BAT_OVERVOLT                   (1UL << 5)
/* 故障位：继电器前母线过压。 */
#define AURORA_FAULT_BUS_OVERVOLT                   (1UL << 6)
/* 故障位：MOS温度超过软件跳闸阈值。 */
#define AURORA_FAULT_MOS_OVERTEMP                   (1UL << 7)
/* 故障位：环境温度超出允许范围。 */
#define AURORA_FAULT_AMB_TEMP                       (1UL << 8)
/* 故障位：ADC测量快照超时或尚未建立。 */
#define AURORA_FAULT_ADC_STALE                      (1UL << 9)
/* 故障位：预充/继电器压差验证失败。 */
#define AURORA_FAULT_RELAY                          (1UL << 10)
/* 故障位：电池档案或外部设置无效。 */
#define AURORA_FAULT_SETTINGS                       (1UL << 11)
/* 故障位：Flash Journal读写失败。 */
#define AURORA_FAULT_STORAGE                        (1UL << 12)
/* 故障位：无法归类的内部一致性错误。 */
#define AURORA_FAULT_INTERNAL                       (1UL << 13)
/* 故障位：ADC DMA硬件报告错误。 */
#define AURORA_FAULT_ADC_DMA                        (1UL << 14)
/* 故障位：DMA覆盖了尚未处理的ADC半缓冲。 */
#define AURORA_FAULT_ADC_OVERRUN                    (1UL << 15)
/* 故障位：Break已锁存但比较器原因无法细分。 */
#define AURORA_FAULT_FAST_BREAK                     (1UL << 16)
/* 故障位：MOS NTC开路、短路或温度超出有效换算范围。 */
#define AURORA_FAULT_MOS_TEMP_INVALID               (1UL << 17)

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

/* 测量值来源质量。 */
typedef enum
{
    AURORA_MEAS_QUALITY_INVALID = 0,                 /* 当前值不可用于控制。 */
    AURORA_MEAS_QUALITY_MEASURED,                    /* 来自独立硬件测量。 */
    AURORA_MEAS_QUALITY_ESTIMATED                    /* 由其他物理量和效率估算。 */
} aurora_measurement_quality_t;

/* 经过滤波、标定和单位换算后的原子测量快照。 */
typedef struct
{
    uint32_t sequence;                               /* 每发布一次完整快照递增。 */
    uint32_t timestamp_ms;                           /* 该快照对应的采样时间。 */
    uint32_t valid_mask;                             /* AURORA_MEAS_VALID_*有效位。 */
    int32_t pv_voltage_mv;                           /* PV输入电压，mV。 */
    int32_t pv_current_ma;                           /* PV输入电流，mA。 */
    int32_t pv_power_mw;                             /* PV输入功率，mW。 */
    int32_t battery_voltage_mv;                      /* 外部电池端电压，mV。 */
    int32_t bus_voltage_mv;                          /* 继电器前Boost母线电压，mV。 */
    int32_t battery_current_est_ma;                  /* 电池电流估算值，mA。 */
    aurora_measurement_quality_t battery_current_quality; /* 电池电流来源质量。 */
    uint8_t battery_current_quality_reserved[3];     /* 显式补齐质量枚举后的字节，避免隐式ABI填充。 */
    int16_t mos_temp_dC;                             /* MOS温度，0.1°C。 */
    int16_t ambient_temp_dC;                         /* 环境温度，0.1°C。 */
} aurora_measurement_t;

/* 单个ADC逻辑通道的线性或NTC标定参数。 */
typedef struct
{
    int32_t gain_num;                                /* 增益分子，输出物理单位/码。 */
    int32_t gain_den;                                /* 增益分母，不得为0。 */
    int32_t offset;                                  /* 换算后的固定物理量偏移。 */
    int16_t zero_code;                               /* 双向电流通道的零电流码值。 */
    int8_t polarity;                                 /* +1正向，-1反向。 */
    bool valid;                                      /* 完成标定且允许参与控制。 */
    uint8_t kind;                                    /* AURORA_ADC_CALIBRATION_*类型。 */
    uint8_t layout_reserved;                         /* 显式补齐标定类型字段。 */
    uint16_t ntc_layout_reserved;                    /* 显式补齐NTC参数起始对齐。 */
    int32_t ntc_pullup_ohm;                          /* NTC上拉电阻，单位ohm。 */
    int32_t ntc_r25_ohm;                              /* NTC在25°C的标称阻值，单位ohm。 */
    int32_t ntc_beta_kelvin;                          /* NTC Beta参数，单位K。 */
    int32_t ntc_full_scale_code;                     /* ADC满量程码值，单位code。 */
    int16_t ntc_reference_temp_dc;                   /* Beta参考温度，0.1°C。 */
    int16_t ntc_min_temp_dc;                          /* 有效换算最低温度，0.1°C。 */
    int16_t ntc_max_temp_dc;                          /* 有效换算最高温度，0.1°C。 */
    int16_t ntc_value_reserved;                       /* 显式补齐标定对象尾部。 */
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

/* 功率级连接与发波状态。 */
typedef enum
{
    AURORA_POWER_OFF = 0,                            /* PWM与继电器均关闭。 */
    AURORA_POWER_WAIT_BATTERY,                       /* 等待有效电池端电压。 */
    AURORA_POWER_PRECHARGE,                          /* 小功率建立母线。 */
    AURORA_POWER_RELAY_SETTLE,                       /* 继电器吸合机械稳定。 */
    AURORA_POWER_RUN,                                /* 正常MPPT充电。 */
    AURORA_POWER_NO_SUN,                             /* 弱光等待并延迟断继电器。 */
    AURORA_POWER_FAULT                               /* 故障关断与放能。 */
} aurora_power_state_t;

/* 由化学体系和电压平台选择的完整充电档案。 */
typedef struct
{
    uint32_t battery_uv_mv;                          /* 电池欠压保护，mV。 */
    uint32_t trickle_exit_mv;                        /* 涓流转恒流阈值，mV。 */
    uint32_t cv_target_mv;                           /* 恒压目标，mV。 */
    uint32_t cv_protect_mv;                          /* 电池绝对过压保护，mV。 */
    uint32_t float_target_mv;                        /* 铅酸浮充目标，mV；其他体系为0。 */
    uint32_t recharge_mv;                            /* 充满后重新允许充电的电压，mV。 */
    uint32_t trickle_current_ma;                     /* 涓流阶段电流上限，mA。 */
    uint32_t cc_current_ma;                          /* 恒流阶段电流上限，mA。 */
    uint32_t tail_current_ma;                        /* CV判满尾流阈值，mA。 */
    uint32_t float_current_ma;                       /* 浮充阶段电流上限，mA。 */
    aurora_battery_chem_t chemistry;                 /* 化学体系。 */
    aurora_battery_pack_t pack;                      /* 标称电压平台。 */
    uint16_t layout_reserved;                        /* 显式补齐枚举字段，保持无隐式填充布局。 */
} aurora_charge_profile_t;

/* 充电状态机对下游功率链给出的限制。 */
typedef struct
{
    uint32_t power_limit_mw;                         /* 允许的最大充电输入功率，mW。 */
    uint32_t voltage_target_mv;                      /* 当前电池电压目标，mV。 */
    aurora_charge_state_t state;                     /* 当前充电阶段。 */
    bool allow_charge;                               /* true表示当前阶段允许能量传输。 */
    bool weak_light;                                 /* true表示PV功率不足，进入弱光策略。 */
    bool power_limited;                              /* true表示电池/温度限制低于MPPT请求。 */
} aurora_charge_output_t;

/* MPPT外环和PV电压PI输出。 */
typedef struct
{
    uint32_t target_voltage_mv;                      /* PV参考电压，mV。 */
    uint32_t theoretical_power_mw;                   /* 未经电池/硬件裁决的功率请求，mW。 */
    bool valid;                                      /* true表示当前样本足以形成控制请求。 */
    uint8_t valid_reserved[3];                       /* 显式补齐有效位后的字节，避免隐式填充。 */
} aurora_mppt_output_t;

/* 应用层提交给应用运行时的硬件无关功率命令。 */
typedef struct
{
    uint16_t duty_q15;                               /* Q6物理占空比，Q15。 */
    aurora_power_state_t state;                      /* 命令对应的功率级状态。 */
    bool pwm_enable;                                 /* true表示请求发波。 */
    bool relay_enable;                               /* true表示请求闭合继电器。 */
    uint8_t state_reserved[3];                       /* 显式补齐功率状态枚举后的字节，避免隐式填充。 */
} aurora_power_command_t;

/* LED逻辑输出。 */
typedef struct
{
    bool led_run_on;                                 /* RUN灯逻辑点亮请求。 */
    bool led_fault_on;                               /* FAULT灯逻辑点亮请求。 */
} aurora_ui_output_t;

/* 需要持久化的用户设置和累计数据。 */
typedef struct
{
    uint32_t lifetime_energy_wh;                     /* 生命周期累计充电能量，Wh。 */
    uint32_t daily_energy_wh;                        /* 本统计日累计充电能量，Wh。 */
    uint32_t settings_revision;                      /* 每次有效设置变更递增。 */
    aurora_battery_chem_t chemistry;                 /* 用户选择的电池化学体系。 */
    aurora_battery_pack_t pack;                      /* 用户选择的电压平台。 */
    uint16_t layout_reserved;                        /* 显式补齐枚举字段，保持存储对象无隐式填充。 */
} aurora_persistent_settings_t;

#ifdef __cplusplus
}
#endif

#endif
