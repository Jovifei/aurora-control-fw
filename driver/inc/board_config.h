#ifndef AURORA_BOARD_CONFIG_H
#define AURORA_BOARD_CONFIG_H

#include <stdint.h>

/*
 * v0.8.3板级硬件配置全部归属Driver层。
 * 最终原理图基线：2026-08-26，G32F031K8T LQFP32，单路异步Boost。
 * APP不得包含本文件；只有driver/src允许消费PinMap、ADC、PWM、Flash和MCU供电参数。
 */

/* MCU启动PVD名义门限，mV；当前2.8V为台架前候选值。 */
#define BOARD_MCU_PVD_THRESHOLD_MV                  (2800UL)
/* PVD数字滤波名义时间，us；64MHz下映射到硬件滤波长度。 */
#define BOARD_MCU_PVD_FILTER_US                     (50UL)
/* PVD使能后等待PVDRDY的最大时间，us；仅判断PVD模块是否正常建立。 */
#define BOARD_MCU_PVD_READY_TIMEOUT_US              (1000UL)
/* VDD连续高于VPVD后必须稳定保持的时间，ms。 */
#define BOARD_MCU_SUPPLY_STABLE_TIME_MS             (100UL)
/* 供电资格阶段软件检查周期，ms。 */
#define BOARD_MCU_SUPPLY_CHECK_PERIOD_MS            (1UL)
/* 禁止PVD直接触发系统复位。 */
#define BOARD_MCU_PVD_RESET_ENABLE                  (0U)
/* 禁止PVD中断作为弱光运行保护。 */
#define BOARD_MCU_PVD_IRQ_ENABLE                    (0U)

/* PA15 / AF3 / ATMR_CH0：低侧Boost门极控制GLC。 */
#define BOARD_PIN_GLC_PORT                          ('A')
#define BOARD_PIN_GLC_NUMBER                        (15U)
#define BOARD_PIN_GLC_AF                            (3U)
/* PA14 / GHC：当前异步硬件不使用，必须固定安全态。 */
#define BOARD_PIN_GHC_UNUSED_PORT                   ('A')
#define BOARD_PIN_GHC_UNUSED_NUMBER                 (14U)
/* PA13：继电器控制。 */
#define BOARD_PIN_RELAY_PORT                        ('A')
#define BOARD_PIN_RELAY_NUMBER                      (13U)
/* PA12：LINK控制。 */
#define BOARD_PIN_LINK_PORT                         ('A')
#define BOARD_PIN_LINK_NUMBER                       (12U)
/* PA10 / PA11：产品UART。 */
#define BOARD_PIN_UART_TX_PORT                      ('A')
#define BOARD_PIN_UART_TX_NUMBER                    (10U)
#define BOARD_PIN_UART_RX_PORT                      ('A')
#define BOARD_PIN_UART_RX_NUMBER                    (11U)
/* PB7 / PB8：调试串口预留。 */
#define BOARD_PIN_DEBUG_TX_PORT                     ('B')
#define BOARD_PIN_DEBUG_TX_NUMBER                   (7U)
#define BOARD_PIN_DEBUG_RX_PORT                     ('B')
#define BOARD_PIN_DEBUG_RX_NUMBER                   (8U)
/* PB9：运行指示灯。 */
#define BOARD_PIN_LED_RUN_PORT                      ('B')
#define BOARD_PIN_LED_RUN_NUMBER                    (9U)
/* PB10 / AF7：COMP0_OUT，开漏方式直接控制U6 EN。 */
#define BOARD_PIN_COMP0_OUT_PORT                    ('B')
#define BOARD_PIN_COMP0_OUT_NUMBER                  (10U)
#define BOARD_PIN_COMP0_OUT_AF                      (7U)
/* U6 EN高有效，因此COMP0故障输出必须拉低。 */
#define BOARD_COMP0_FAULT_ACTIVE_LOW                (1U)
/* PB11：故障指示灯。 */
#define BOARD_PIN_LED_FAULT_PORT                    ('B')
#define BOARD_PIN_LED_FAULT_NUMBER                  (11U)

/* ADC规则组扫描顺序；必须与drv_adc.c和measurement.c完全一致。 */
#define BOARD_ADC_INDEX_PV_I                        (0U)
#define BOARD_ADC_INDEX_PV_U                        (1U)
#define BOARD_ADC_INDEX_BAT_U                       (2U)
#define BOARD_ADC_INDEX_BUS_U                       (3U)
#define BOARD_ADC_INDEX_NTC_MOS                     (4U)
#define BOARD_ADC_INDEX_NTC_AMB                     (5U)
/* G32 ADC物理通道号。 */
#define BOARD_ADC_CH_PV_I                           (1U)
#define BOARD_ADC_CH_PV_U                           (2U)
#define BOARD_ADC_CH_BAT_U                          (3U)
#define BOARD_ADC_CH_BUS_U                          (4U)
#define BOARD_ADC_CH_NTC_MOS                        (5U)
#define BOARD_ADC_CH_NTC_AMB                        (6U)
/* ADC就绪等待循环上限；只用于初始化失败判定。 */
#define BOARD_ADC_READY_TIMEOUT_LOOPS               (100000UL)

/* 12位ADC满量程码值。 */
#define BOARD_ADC_FULL_SCALE_CODE                   (4095L)
/* 模拟参考电压名义值，mV；实板标定前不得改为已验证。 */
#define BOARD_ADC_REFERENCE_MV                      (3300L)
/* PV_I：3mΩ分流器×内部16倍OPA，约16.79mA/码。 */
#define BOARD_ADC_PV_I_GAIN_NUM                     (16790L)
#define BOARD_ADC_PV_I_GAIN_DEN                     (1000L)
#define BOARD_ADC_PV_I_ZERO_CODE                    (2048)
#define BOARD_ADC_PV_I_POLARITY                     (-1)
/* PV_U：75k/3k分压，比例26。 */
#define BOARD_ADC_PV_U_DIVIDER_NUM                  (26L)
#define BOARD_ADC_PV_U_DIVIDER_DEN                  (1L)
/* BAT_U：15M/510k分压，比例15510/510。 */
#define BOARD_ADC_BAT_U_DIVIDER_NUM                 (15510L)
#define BOARD_ADC_BAT_U_DIVIDER_DEN                 (510L)
/* BST_U：125k/5k分压，比例26。 */
#define BOARD_ADC_BUS_U_DIVIDER_NUM                 (26L)
#define BOARD_ADC_BUS_U_DIVIDER_DEN                 (1L)

/* ATMR计数时钟，Hz。 */
#define BOARD_PWM_TIMER_CLOCK_HZ                    (64000000UL)
/* 50kHz对应周期计数值。 */
#define BOARD_PWM_PERIOD_COUNTS                     (1280U)
/* 使用ATMR通道0输出GLC。 */
#define BOARD_PWM_CHANNEL                           (0U)
/* GLC高电平使低侧MOS导通。 */
#define BOARD_PWM_ACTIVE_HIGH                       (1U)
/* CCR写入必须经preload在自然UPDATE边界生效。 */
#define BOARD_PWM_CCR_PRELOAD                       (1U)
/* ARR启用preload。 */
#define BOARD_PWM_ARR_PRELOAD                       (1U)
/* Break解除后禁止Automatic Output自动恢复。 */
#define BOARD_PWM_AUTOMATIC_OUTPUT                  (0U)
/* 最大物理占空比Q15，约90%；最终以低压台架为准。 */
#define BOARD_PWM_MAX_DUTY_Q15                      (29491U)

/* G32F031内部Flash物理擦除页大小，字节。 */
#define BOARD_FLASH_PAGE_SIZE                       (512UL)
/* 双页Journal A页地址。 */
#define BOARD_FLASH_PAGE_A_ADDRESS                  (0x0000FC00UL)
/* 双页Journal B页地址。 */
#define BOARD_FLASH_PAGE_B_ADDRESS                  (0x0000FE00UL)

/* PinMap已经人工复核。 */
#define BOARD_GATE_PINMAP_REVIEWED                  (1U)
/* COMP路由/极性尚未完成实板强制触发验收。 */
#define BOARD_GATE_COMP_ROUTE_VALIDATED             (0U)
/* ADC/OPA模拟标定尚未闭环。 */
#define BOARD_GATE_ANALOG_CALIBRATED                (0U)
/* Keil AC6真实链接/MAP尚未作为当前候选验收证据。 */
#define BOARD_GATE_KEIL_LINKED                      (0U)
/* 低压功率台架尚未完成。 */
#define BOARD_GATE_LOW_VOLTAGE_BENCH                (0U)
/* 最终人工功率总门，当前必须保持0。 */
#define BOARD_POWER_OUTPUT_ALLOWED                  (0U)

/* 两路LED均为低电平点亮。 */
#define BOARD_LED_ACTIVE_LOW                        (1U)
/* 继电器控制信号为高电平有效。 */
#define BOARD_RELAY_ACTIVE_HIGH                     (1U)
/* 产品UART波特率。 */
#define BOARD_UART_BAUDRATE                         (115200UL)
/* Driver层TX环形缓冲长度。 */
#define BOARD_UART_TX_BUFFER_SIZE                   (256U)

/* IWDT名义低速时钟，Hz；实板受LSI容差影响。 */
#define BOARD_WATCHDOG_CLOCK_HZ                     (40000UL)
/* IWDT固定预分频值。 */
#define BOARD_WATCHDOG_PRESCALER                    (64UL)
/* 毫秒换算秒的比例。 */
#define BOARD_MILLISECONDS_PER_SECOND               (1000UL)
/* IWDT 12位重载寄存器最大值。 */
#define BOARD_WATCHDOG_RELOAD_MAX                   (0x0FFFU)
/* 等待IWDT寄存器更新完成的循环上限。 */
#define BOARD_WATCHDOG_READY_TIMEOUT_LOOPS          (100000UL)

#endif
