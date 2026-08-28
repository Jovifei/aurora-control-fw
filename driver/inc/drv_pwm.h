#ifndef AURORA_DRV_PWM_H
#define AURORA_DRV_PWM_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化带CCR预装载和Break安全链的Boost PWM。 */
bool drv_pwm_init(void);
/* 在故障ISR中立即关闭主输出。 */
void drv_pwm_force_off_isr(void);
/* 在Break ISR中屏蔽重复中断但不清除硬件锁存。 */
void drv_pwm_quiesce_break_irq_isr(void);
/* 关闭软件发波授权并清零输出。 */
void drv_pwm_disarm(void);
/* 提交零占空比并返回自然UEV等待序号。 */
bool drv_pwm_prepare_arm_zero(uint32_t *sequence);
/* 只写CCR shadow，运行期不得触发软件UG。 */
bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence);
/* 在安全复核后打开MOE授权。 */
bool drv_pwm_arm(void);
/* 查询当前PWM输出是否有效。 */
bool drv_pwm_output_active(void);
/* 查询外部Break源状态。 */
bool drv_pwm_break_source_active(void);
/* 查询硬件Break锁存状态。 */
bool drv_pwm_break_latched(void);
/* 清除Break锁存，供独立的人工恢复流程调用。 */
bool drv_pwm_clear_break_latch(void);
/* 查询已自然UEV应用的CCR序号。 */
uint32_t drv_pwm_applied_sequence(void);
/* 在ISR中读取ATMR共享中断待处理位。 */
uint8_t drv_pwm_irq_pending_isr(void);
/* 应答一次性零CCR自然UEV。 */
void drv_pwm_update_isr_ack(void);

#ifdef __cplusplus
}
#endif

#endif
