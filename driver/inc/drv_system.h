#ifndef AURORA_DRV_SYSTEM_H
#define AURORA_DRV_SYSTEM_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化系统时钟和毫秒节拍。 */
void drv_system_init(void);
/* 读取当前毫秒时间。 */
uint32_t drv_time_now_ms(void);
/* 在SysTick ISR中递增系统时间。 */
void drv_time_tick_isr(void);
/* 保存并屏蔽中断。 */
aurora_irq_state_t drv_irq_save(void);
/* 恢复此前的中断状态。 */
void drv_irq_restore(aurora_irq_state_t state);
/* 配置中断优先级。 */
void drv_irq_configure_priorities(void);
/* 触发目标系统复位；Host模拟会返回。 */
DRV_SYSTEM_NORETURN void drv_system_reset(void);
/* 等待下一次中断，应用入口不直接包含芯片头文件。 */
void drv_wait_for_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif
