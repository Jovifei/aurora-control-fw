#ifndef AURORA_DRV_PWM_H
#define AURORA_DRV_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* Q15中100%占空比标度。 */
#define DRV_DUTY_Q15_ONE                            (32768U)

void BSP_PWM_Init(void);
void BSP_PWM_Start(void);
void BSP_PWM_Stop(void);
void BSP_PWM_SetDuty(uint16_t permille);
void BSP_PWM_SetFrequency(uint32_t frequency_hz);
void BSP_PWM_SetComplementary(uint32_t enable);
uint32_t BSP_PWM_IsBraked(void);

bool drv_pwm_init(void);
void drv_pwm_force_off_isr(void);
void drv_pwm_quiesce_break_irq_isr(void);
void drv_pwm_disarm(void);
bool drv_pwm_prepare_arm_zero(uint32_t *sequence);
bool drv_pwm_stage_duty(uint16_t duty_q15, uint32_t *sequence);
bool drv_pwm_arm(void);
bool drv_pwm_output_active(void);
bool drv_pwm_break_source_active(void);
bool drv_pwm_break_latched(void);
bool drv_pwm_clear_break_latch(void);
uint32_t drv_pwm_applied_sequence(void);
void drv_pwm_update_isr_ack(void);

#endif
