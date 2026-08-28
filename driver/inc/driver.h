#ifndef AURORA_DRIVER_H
#define AURORA_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADC逻辑通道数，必须与板级扫描顺序一致。 */
#define DRV_ADC_CHANNEL_COUNT                       (6U)
/* 每个DMA半缓冲的完整扫描次数。 */
#define DRV_ADC_SCANS_PER_BLOCK                     (16U)
/* 单个DMA完成块的16位字数。 */
#define DRV_ADC_BLOCK_WORDS                         (DRV_ADC_CHANNEL_COUNT * \
                                                     DRV_ADC_SCANS_PER_BLOCK)
/* Q15中100%占空比标度。 */
#define DRV_DUTY_Q15_ONE                            (32768U)
/* ATMR共享中断待处理位：Break事件。 */
#define DRV_PWM_IRQ_BREAK                            (1U << 0)
/* ATMR共享中断待处理位：一次性零CCR Update事件。 */
#define DRV_PWM_IRQ_UPDATE                           (1U << 1)
/* 驱动故障位：MOS支路快速过流。 */
#define DRV_FAULT_MOS_OCP                           (1UL << 0)
/* 驱动故障位：PV输入快速过流。 */
#define DRV_FAULT_PV_OCP                            (1UL << 1)
/* ADC DMA结果位：半缓冲0完成。 */
#define DRV_ADC_IRQ_BLOCK0                          (1U << 0)
/* ADC DMA结果位：半缓冲1完成。 */
#define DRV_ADC_IRQ_BLOCK1                          (1U << 1)
/* ADC DMA结果位：传输错误。 */
#define DRV_ADC_IRQ_ERROR                           (1U << 2)

/* 保存PRIMASK的类型。 */
typedef uint32_t aurora_irq_state_t;

#if defined(G32F031xx)
/* 目标复位调用不会返回，供ArmClang进行控制流分析。 */
#define DRV_SYSTEM_NORETURN __attribute__((noreturn))
#else
/* Host mock需要继续执行测试，不能继承目标的noreturn属性。 */
#define DRV_SYSTEM_NORETURN
#endif

/* 各驱动模块的公开契约；具体实现文件只包含对应模块头。 */
#include "drv_adc.h"
#include "drv_board.h"
#include "drv_comp.h"
#include "drv_flash.h"
#include "drv_io.h"
#include "drv_pwm.h"
#include "drv_system.h"
#include "drv_uart.h"
#include "drv_watchdog.h"

#ifdef __cplusplus
}
#endif

#endif
