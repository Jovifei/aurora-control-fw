#ifndef AURORA_APP_CONFIG_H
#define AURORA_APP_CONFIG_H

#include <stdint.h>

/* 默认高功率 BOM；低功率 BOM 只需切换编译宏，不复制业务代码。 */
#define AURORA_PROFILE_HIGH_POWER       (1U)
#define AURORA_PROFILE_LOW_POWER        (2U)
#ifndef AURORA_POWER_PROFILE
#define AURORA_POWER_PROFILE            AURORA_PROFILE_HIGH_POWER
#endif

#if AURORA_POWER_PROFILE == AURORA_PROFILE_HIGH_POWER
#define AURORA_RATED_POWER_MW           (300000U)
#define AURORA_PV_CURRENT_LIMIT_MA       (12000L)
#define AURORA_PRECHARGE_POWER_MW        (15000U)
#define AURORA_PRECHARGE_CURRENT_MA      (1000L)
#define AURORA_PRODUCT_MODEL             "0300"
#else
#define AURORA_RATED_POWER_MW           (120000U)
#define AURORA_PV_CURRENT_LIMIT_MA       (7000L)
#define AURORA_PRECHARGE_POWER_MW        (8000U)
#define AURORA_PRECHARGE_CURRENT_MA      (600L)
#define AURORA_PRODUCT_MODEL             "0120"
#endif

#define AURORA_PV_START_MIN_MV           (13000L)
#define AURORA_PV_ABSOLUTE_MAX_MV        (55000L)
#define AURORA_MEASUREMENT_STALE_MS      (50U)
#define AURORA_MOS_TRIP_TEMP_DC          (900)
#define AURORA_MOS_RECOVER_TEMP_DC       (800)
#define AURORA_AMB_MIN_TEMP_DC           (-400)
#define AURORA_AMB_MAX_TEMP_DC           (850)
#define AURORA_AMB_RECOVER_MARGIN_DC      (50)

#define AURORA_RELAY_CLOSE_DELTA_MV      (1500L)
#define AURORA_RELAY_VERIFY_DELTA_MV     (2500L)
#define AURORA_RELAY_DELTA_HOLD_MS       (1000U)
#define AURORA_RELAY_SETTLE_MS           (100U)
#define AURORA_RELAY_FAULT_RELEASE_MS     (20U)
#define AURORA_PRECHARGE_TIMEOUT_MS      (30000U)
#define AURORA_NO_SUN_ENTER_MW           (1000L)
#define AURORA_NO_SUN_RECOVER_MW         (3000L)
#define AURORA_NO_SUN_OPEN_RELAY_MS      (30UL * 60UL * 1000U)

#define AURORA_DUTY_MIN_Q15              (655U)   /* 约 2% */
#define AURORA_DUTY_MAX_Q15              (29491U) /* 约 90% */
#define AURORA_DUTY_STEP_Q15             (256U)
#define AURORA_PWM_FREQUENCY_HZ          (50000U)

#define AURORA_MPPT_UPDATE_MS            (80U)
#define AURORA_MPPT_PI_UPDATE_MS         (10U)
#define AURORA_MPPT_V_NOISE_MV           (50L)
#define AURORA_MPPT_P_NOISE_MW           (500L)
#define AURORA_MPPT_STEP_MIN_MV          (50L)
#define AURORA_MPPT_STEP_MAX_MV          (500L)
#define AURORA_MPPT_VOC_MARGIN_MV        (500L)

#define AURORA_MAX_CHARGE_TIME_MS        (900UL * 60UL * 1000U)
#define AURORA_FLOAT_TIME_MS             (180UL * 60UL * 1000U)
#define AURORA_TAIL_HOLD_MS              (1000U)

#define AURORA_FW_VERSION_MAJOR          (2U)
#define AURORA_FW_VERSION_MINOR          (0U)
#define AURORA_FW_VERSION_PATCH          (0U)

#define AURORA_WATCHDOG_WINDOW_MS        (100U)
#define AURORA_WATCHDOG_TIMEOUT_MS       (1000U)
#define AURORA_WATCHDOG_STARTUP_GRACE_MS (500U)

/* 主循环单次最多消费的串口字节数，避免长帧长期占用控制调度。 */
#define AURORA_UART_SERVICE_RX_BUDGET    (64U)

#endif
