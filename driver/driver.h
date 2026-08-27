#ifndef AURORA_DRIVER_H
#define AURORA_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


#define DRV_ADC_CHANNEL_COUNT        (6U)
#define DRV_ADC_SCANS_PER_BLOCK      (16U)
#define DRV_ADC_BLOCK_WORDS          (DRV_ADC_CHANNEL_COUNT * DRV_ADC_SCANS_PER_BLOCK)
#define DRV_DUTY_Q15_ONE             (32768U)
#define DRV_FAULT_MOS_OCP            (1UL << 0)
#define DRV_FAULT_PV_OCP             (1UL << 1)
#define DRV_ADC_IRQ_BLOCK0            (1U << 0)
#define DRV_ADC_IRQ_BLOCK1            (1U << 1)
#define DRV_ADC_IRQ_ERROR             (1U << 2)

typedef uint32_t aurora_irq_state_t;

void drv_system_init(void);
uint32_t drv_time_now_ms(void);
void drv_time_tick_isr(void);
aurora_irq_state_t drv_irq_save(void);
void drv_irq_restore(aurora_irq_state_t state);
void drv_irq_configure_priorities(void);
void drv_system_reset(void);

bool drv_adc_init(void);
bool drv_adc_start(void);
const uint16_t *drv_adc_completed_block(uint8_t block_index);
size_t drv_adc_block_words(void);
uint8_t drv_adc_dma_irq_ack(void);

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

bool drv_comp_init(void);
uint32_t drv_comp_fault_mask(void);
void drv_comp_irq_ack(void);

void drv_io_init(void);
void drv_io_set_relay(bool on);
void drv_io_set_link(bool on);
void drv_io_set_leds(bool run_on, bool fault_on);

bool drv_uart_init(void);
bool drv_uart_send(const uint8_t *data, size_t length);
bool drv_uart_tx_busy(void);
bool drv_uart_rx_ready_isr(void);
uint8_t drv_uart_read_isr(void);
void drv_uart_tx_isr(void);
void drv_uart_irq_ack(void);

bool drv_flash_read(uint32_t address, void *data, size_t length);
bool drv_flash_erase_page(uint32_t address);
bool drv_flash_program(uint32_t address, const void *data, size_t length);

bool drv_watchdog_init(uint32_t timeout_ms);
void drv_watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif
