#ifndef AURORA_APP_TYPES_H
#define AURORA_APP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 应用层统一使用带单位的整数，避免把 ADC 码、定时器 CCR 与物理量混在一起。
 * 电压：mV；电流：mA；功率：mW；温度：0.1 摄氏度；占空比：Q15。
 */
#define AURORA_DUTY_Q15_ONE             (32768U)
#define AURORA_ADC_CHANNEL_COUNT        (6U)
#define AURORA_ADC_SCANS_PER_BLOCK      (16U)
#define AURORA_ADC_BLOCK_WORDS          (AURORA_ADC_CHANNEL_COUNT * AURORA_ADC_SCANS_PER_BLOCK)
#define AURORA_PROTOCOL_MAX_DATA        (127U)

#define AURORA_MEAS_VALID_PV_V          (1UL << 0)
#define AURORA_MEAS_VALID_PV_I          (1UL << 1)
#define AURORA_MEAS_VALID_BAT_V         (1UL << 2)
#define AURORA_MEAS_VALID_BUS_V         (1UL << 3)
#define AURORA_MEAS_VALID_MOS_TEMP      (1UL << 4)
#define AURORA_MEAS_VALID_AMB_TEMP      (1UL << 5)
#define AURORA_MEAS_VALID_PV_POWER      (1UL << 6)
#define AURORA_MEAS_VALID_BAT_I_EST     (1UL << 7)

#define AURORA_FAULT_FAST_MOS_OCP       (1UL << 0)
#define AURORA_FAULT_FAST_PV_OCP        (1UL << 1)
#define AURORA_FAULT_PV_UNDERVOLT       (1UL << 2)
#define AURORA_FAULT_PV_OVERVOLT        (1UL << 3)
#define AURORA_FAULT_BAT_UNDERVOLT      (1UL << 4)
#define AURORA_FAULT_BAT_OVERVOLT       (1UL << 5)
#define AURORA_FAULT_BUS_OVERVOLT       (1UL << 6)
#define AURORA_FAULT_MOS_OVERTEMP       (1UL << 7)
#define AURORA_FAULT_AMB_TEMP           (1UL << 8)
#define AURORA_FAULT_ADC_STALE          (1UL << 9)
#define AURORA_FAULT_RELAY              (1UL << 10)
#define AURORA_FAULT_SETTINGS           (1UL << 11)
#define AURORA_FAULT_STORAGE            (1UL << 12)
#define AURORA_FAULT_INTERNAL           (1UL << 13)
#define AURORA_FAULT_ADC_DMA            (1UL << 14)
#define AURORA_FAULT_ADC_OVERRUN        (1UL << 15)
#define AURORA_FAULT_FAST_BREAK         (1UL << 16)

typedef enum
{
    AURORA_STATUS_OK = 0,
    AURORA_STATUS_BUSY,
    AURORA_STATUS_INVALID,
    AURORA_STATUS_NOT_READY,
    AURORA_STATUS_IO_ERROR,
    AURORA_STATUS_DENIED
} aurora_status_t;

typedef enum
{
    AURORA_CHEM_LEAD = 0,
    AURORA_CHEM_TERNARY,
    AURORA_CHEM_LFP,
    AURORA_CHEM_SODIUM,
    AURORA_CHEM_COUNT
} aurora_battery_chem_t;

typedef enum
{
    AURORA_PACK_48V = 0,
    AURORA_PACK_60V,
    AURORA_PACK_72V,
    AURORA_PACK_COUNT
} aurora_battery_pack_t;

typedef enum
{
    AURORA_MEAS_QUALITY_INVALID = 0,
    AURORA_MEAS_QUALITY_MEASURED,
    AURORA_MEAS_QUALITY_ESTIMATED
} aurora_measurement_quality_t;

typedef struct
{
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t valid_mask;
    int32_t pv_voltage_mv;
    int32_t pv_current_ma;
    int32_t pv_power_mw;
    int32_t battery_voltage_mv;
    int32_t bus_voltage_mv;
    int32_t battery_current_est_ma;
    int16_t mos_temp_dC;
    int16_t ambient_temp_dC;
    aurora_measurement_quality_t battery_current_quality;
} aurora_measurement_t;

typedef struct
{
    int32_t gain_num;
    int32_t gain_den;
    int32_t offset;
    int16_t zero_code;
    int8_t polarity;
    bool valid;
} aurora_adc_calibration_t;

typedef struct
{
    aurora_adc_calibration_t channel[AURORA_ADC_CHANNEL_COUNT];
} aurora_measurement_calibration_t;

typedef enum
{
    AURORA_CHARGE_OFF = 0,
    AURORA_CHARGE_TRICKLE,
    AURORA_CHARGE_CC,
    AURORA_CHARGE_CV,
    AURORA_CHARGE_FLOAT,
    AURORA_CHARGE_COMPLETE,
    AURORA_CHARGE_FAULT
} aurora_charge_state_t;

typedef enum
{
    AURORA_POWER_OFF = 0,
    AURORA_POWER_WAIT_BATTERY,
    AURORA_POWER_PRECHARGE,
    AURORA_POWER_RELAY_SETTLE,
    AURORA_POWER_RUN,
    AURORA_POWER_NO_SUN,
    AURORA_POWER_FAULT
} aurora_power_state_t;

typedef struct
{
    aurora_battery_chem_t chemistry;
    aurora_battery_pack_t pack;
    uint32_t battery_uv_mv;
    uint32_t trickle_exit_mv;
    uint32_t cv_target_mv;
    uint32_t cv_protect_mv;
    uint32_t float_target_mv;
    uint32_t recharge_mv;
    uint32_t trickle_current_ma;
    uint32_t cc_current_ma;
    uint32_t tail_current_ma;
    uint32_t float_current_ma;
} aurora_charge_profile_t;

typedef struct
{
    bool allow_charge;
    bool weak_light;
    bool power_limited;
    uint32_t power_limit_mw;
    uint32_t voltage_target_mv;
    aurora_charge_state_t state;
} aurora_charge_output_t;

typedef struct
{
    uint32_t target_voltage_mv;
    uint32_t theoretical_power_mw;
    bool valid;
} aurora_mppt_output_t;

typedef struct
{
    bool pwm_enable;
    bool relay_enable;
    uint16_t duty_q15;
    aurora_power_state_t state;
} aurora_power_command_t;

typedef struct
{
    bool led_run_on;
    bool led_fault_on;
} aurora_ui_output_t;

typedef struct
{
    aurora_battery_chem_t chemistry;
    aurora_battery_pack_t pack;
    uint32_t lifetime_energy_wh;
    uint32_t daily_energy_wh;
    uint32_t settings_revision;
} aurora_persistent_settings_t;

#ifdef __cplusplus
}
#endif

#endif
