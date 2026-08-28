#ifndef AURORA_DRV_SYSTEM_H
#define AURORA_DRV_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

/* 保存PRIMASK的类型。 */
typedef uint32_t aurora_irq_state_t;

#if defined(G32F031xx)
/* 目标复位调用不会返回，供ArmClang进行控制流分析。 */
#define DRV_SYSTEM_NORETURN                         __attribute__((noreturn))
#else
/* Host mock需要继续执行测试，不能继承目标noreturn属性。 */
#define DRV_SYSTEM_NORETURN
#endif

void drv_system_init(void);
uint32_t drv_time_now_ms(void);
void drv_time_tick_isr(void);
aurora_irq_state_t drv_irq_save(void);
void drv_irq_restore(aurora_irq_state_t state);
void drv_irq_configure_priorities(void);
bool drv_system_supply_qualifier_init(void);
bool drv_system_supply_monitor_ready(void);
bool drv_system_supply_is_good(void);
bool drv_system_wait_for_supply_stable(void);
void drv_system_supply_qualifier_stop(void);
DRV_SYSTEM_NORETURN void drv_system_reset(void);

#endif
