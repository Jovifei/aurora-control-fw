#ifndef AURORA_BOARD_CONFIG_H
#define AURORA_BOARD_CONFIG_H

#include <stdint.h>

/*
 * 应用业务模块不得包含本文件；引脚、外设、标定和功率门禁只允许在driver使用，
 * 应用组合根main.h仅通过驱动契约使用其中的缓冲区和路由配置。
 */

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
/* PA12：Link控制。 */
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
/* USART路由模式：默认使用PA10/PA11承载蓝牙数据。 */
#define BOARD_USART_MODE_BLUETOOTH                  (0U)
/* USART路由模式：切换到PB7/PB8承载GE_DEBUG日志。 */
#define BOARD_USART_MODE_DEBUG                      (1U)
/* 当前构建使用的USART路由；Keil/CMake可用编译宏覆盖。 */
#ifndef BOARD_USART_MODE
#define BOARD_USART_MODE                            BOARD_USART_MODE_BLUETOOTH
#endif
#if (BOARD_USART_MODE != BOARD_USART_MODE_BLUETOOTH) && \
    (BOARD_USART_MODE != BOARD_USART_MODE_DEBUG)
#error "BOARD_USART_MODE must select Bluetooth or Debug route"
#endif
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
#define BOARD_ADC_CH_PV_I                           (1U)  /* PA8。 */
#define BOARD_ADC_CH_PV_U                           (2U)  /* PA9。 */
#define BOARD_ADC_CH_BAT_U                          (3U)  /* PB0。 */
#define BOARD_ADC_CH_BUS_U                          (4U)  /* PB1。 */
#define BOARD_ADC_CH_NTC_MOS                        (5U)  /* PB12。 */
#define BOARD_ADC_CH_NTC_AMB                        (6U)  /* PB5。 */
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
/* 板载MOS NTC的分压上拉电阻，单位ohm；对应原理图R37=5.1K。 */
#define BOARD_NTC_MOS_PULLUP_OHM                    (5100L)
/* 板载MOS NTC的25°C标称阻值，单位ohm；对应原理图R42=100K。 */
#define BOARD_NTC_MOS_R25_OHM                       (100000L)
/* 板载MOS NTC的Beta参数，单位K；对应原理图R42=3950K。 */
#define BOARD_NTC_MOS_BETA_KELVIN                   (3950L)
/* Beta公式参考温度，单位0.1°C；固定为25.0°C。 */
#define BOARD_NTC_MOS_REFERENCE_TEMP_DC             (250)
/* MOS NTC查表最低有效温度，单位0.1°C。 */
#define BOARD_NTC_MOS_MIN_TEMP_DC                   (-400)
/* MOS NTC查表最高有效温度，单位0.1°C。 */
#define BOARD_NTC_MOS_MAX_TEMP_DC                   (1250)

/* ATMR计数时钟，单位Hz。 */
#define BOARD_PWM_TIMER_CLOCK_HZ                    (64000000UL)
/* 50kHz对应的周期计数值。 */
#define BOARD_PWM_PERIOD_COUNTS                     (1280U)
/* 使用ATMR通道0输出GLC。 */
#define BOARD_PWM_CHANNEL                           (0U)
/* GLC高电平使低侧MOS导通。 */
#define BOARD_PWM_ACTIVE_HIGH                       (1U)
/* CCR写入必须经预装载在自然UPDATE边界生效。 */
#define BOARD_PWM_CCR_PRELOAD                       (1U)
/* ARR同样启用预装载。 */
#define BOARD_PWM_ARR_PRELOAD                       (1U)
/* Break解除后禁止自动恢复MOE。 */
#define BOARD_PWM_AUTOMATIC_OUTPUT                  (0U)
/* 物理Q6最大占空比，Q15，约90%；最终以低压台架为准。 */
#define BOARD_PWM_MAX_DUTY_Q15                      (29491U)

/* G32F031内部Flash物理擦除页大小，单位字节。 */
#define BOARD_FLASH_PAGE_SIZE                       (512UL)
/* 双页Journal A页地址，位于应用镜像保留区。 */
#define BOARD_FLASH_PAGE_A_ADDRESS                  (0x0000FC00UL)
/* 双页Journal B页地址，位于应用镜像保留区。 */
#define BOARD_FLASH_PAGE_B_ADDRESS                  (0x0000FE00UL)

/*
 * 软件门禁状态说明：
 * - PinMap已经依据最终原理图人工复核，保持1；
 * - COMP路由、模拟标定、Keil链接和低压台架尚无闭环证据，保持0；
 * - BOARD_POWER_OUTPUT_ALLOWED是最终总门，当前必须为0。
 */
#define BOARD_GATE_PINMAP_REVIEWED                  (1U)
#define BOARD_GATE_COMP_ROUTE_VALIDATED             (0U)
#define BOARD_GATE_ANALOG_CALIBRATED                (0U)
#define BOARD_GATE_KEIL_LINKED                      (0U)
#define BOARD_GATE_LOW_VOLTAGE_BENCH                (0U)
#define BOARD_POWER_OUTPUT_ALLOWED                  (0U)

/* 两路LED均为低电平点亮。 */
#define BOARD_LED_ACTIVE_LOW                        (1U)
/* 继电器控制信号为高电平有效。 */
#define BOARD_RELAY_ACTIVE_HIGH                     (1U)
/* 产品UART波特率。 */
#define BOARD_UART_BAUDRATE                         (115200UL)
/* 单次USART ISR最多搬运的RX字节数。 */
#define BOARD_UART_ISR_RX_BUDGET                    (32U)
/* 应用主循环单次最多消费的RX字节数。 */
#define BOARD_UART_APP_RX_BUDGET                    (64U)
/* 驱动层TX环形缓冲长度，必须能容纳至少一个最大协议帧。 */
#define BOARD_UART_TX_BUFFER_SIZE                   (256U)
/* 应用运行时RX环形缓冲长度。 */
#define BOARD_UART_RX_BUFFER_SIZE                   (256U)

/* IWDT名义低速时钟，单位Hz；实板受LSI容差影响。 */
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
