#ifndef AURORA_BOARD_CONFIG_H
#define AURORA_BOARD_CONFIG_H

#include <stdint.h>

/*
 * 最新原理图（2026-08-26，LQFP32）唯一板级绑定。
 * 应用层不得包含本文件；修改引脚只允许影响 board/ 与 driver/。
 */
#define BOARD_PIN_GLC_PORT                  ('A')
#define BOARD_PIN_GLC_NUMBER                (15U)
#define BOARD_PIN_GLC_AF                    (3U)  /* PA15 / AF3 / ATMR_CH0 */
#define BOARD_PIN_GHC_UNUSED_PORT           ('A')
#define BOARD_PIN_GHC_UNUSED_NUMBER         (14U)
#define BOARD_PIN_RELAY_PORT                ('A')
#define BOARD_PIN_RELAY_NUMBER              (13U)
#define BOARD_PIN_LINK_PORT                 ('A')
#define BOARD_PIN_LINK_NUMBER               (12U)
#define BOARD_PIN_UART_TX_PORT              ('A')
#define BOARD_PIN_UART_TX_NUMBER            (10U)
#define BOARD_PIN_UART_RX_PORT              ('A')
#define BOARD_PIN_UART_RX_NUMBER            (11U)
#define BOARD_PIN_DEBUG_TX_PORT             ('B')
#define BOARD_PIN_DEBUG_TX_NUMBER           (7U)
#define BOARD_PIN_DEBUG_RX_PORT             ('B')
#define BOARD_PIN_DEBUG_RX_NUMBER           (8U)
#define BOARD_PIN_LED_RUN_PORT              ('B')
#define BOARD_PIN_LED_RUN_NUMBER            (9U)
#define BOARD_PIN_COMP0_OUT_PORT             ('B')
#define BOARD_PIN_COMP0_OUT_NUMBER           (10U)
#define BOARD_PIN_COMP0_OUT_AF               (7U)  /* PB10 / AF7 / COMP0_OUT */
#define BOARD_COMP0_FAULT_ACTIVE_LOW          (1U)  /* U6 EN高有效：故障时COMP0_O拉低 */
#define BOARD_PIN_LED_FAULT_PORT             ('B')
#define BOARD_PIN_LED_FAULT_NUMBER           (11U)

/* ADC规则组扫描顺序：必须与 driver/drv_adc.c 和 app/measurement.c 一致。 */
#define BOARD_ADC_CH_PV_I                   (1U)  /* PA8  */
#define BOARD_ADC_CH_PV_U                   (2U)  /* PA9  */
#define BOARD_ADC_CH_BAT_U                  (3U)  /* PB0  */
#define BOARD_ADC_CH_BUS_U                  (4U)  /* PB1  */
#define BOARD_ADC_CH_NTC_MOS                (5U)  /* PB12 */
#define BOARD_ADC_CH_NTC_AMB                (6U)  /* PB5  */

#define BOARD_PWM_TIMER_CLOCK_HZ            (64000000UL)
#define BOARD_PWM_PERIOD_COUNTS             (1280U)
#define BOARD_PWM_CHANNEL                   (0U)
#define BOARD_PWM_ACTIVE_HIGH               (1U)
#define BOARD_PWM_CCR_PRELOAD               (1U)
#define BOARD_PWM_ARR_PRELOAD               (1U)
#define BOARD_PWM_AUTOMATIC_OUTPUT          (0U)
#define BOARD_PWM_MAX_DUTY_Q15              (29491U) /* 约90%，最终以低压台架为准 */

#define BOARD_FLASH_PAGE_SIZE               (512UL)
#define BOARD_FLASH_PAGE_A_ADDRESS          (0x0000FC00UL)
#define BOARD_FLASH_PAGE_B_ADDRESS          (0x0000FE00UL)

/*
 * 原理图和官方数据手册已经确认：PB10/AF7 = COMP0_OUT，并外接 U6 EN。
 * EN/Break有效电平已按“正常高、故障低”实现，但尚未完成台架强制触发验证，
 * 因而COMP_ROUTE门禁继续保持0；不得仅凭原理图把它改为1。
 */
#define BOARD_GATE_PINMAP_REVIEWED          (1U)
#define BOARD_GATE_COMP_ROUTE_VALIDATED     (0U)
#define BOARD_GATE_ANALOG_CALIBRATED        (0U)
#define BOARD_GATE_KEIL_LINKED              (0U)
#define BOARD_GATE_LOW_VOLTAGE_BENCH        (0U)
#define BOARD_POWER_OUTPUT_ALLOWED          (0U)

#define BOARD_LED_ACTIVE_LOW                (1U)
#define BOARD_RELAY_ACTIVE_HIGH             (1U)
#define BOARD_UART_BAUDRATE                 (115200UL)
#define BOARD_UART_ISR_RX_BUDGET            (32U)
#define BOARD_UART_SERVICE_RX_BUDGET        (64U)

#endif
