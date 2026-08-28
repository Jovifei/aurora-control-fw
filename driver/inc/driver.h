#ifndef AURORA_DRIVER_H
#define AURORA_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADC逻辑通道数，必须与board扫描顺序一致。 */
#define DRV_ADC_CHANNEL_COUNT                       (6U)
/* 每个DMA半缓冲的完整扫描次数。 */
#define DRV_ADC_SCANS_PER_BLOCK                     (16U)
/* 单个DMA完成块的16位字数。 */
#define DRV_ADC_BLOCK_WORDS                         (DRV_ADC_CHANNEL_COUNT * \
                                                     DRV_ADC_SCANS_PER_BLOCK)
/* Q15中100%占空比标度。 */
#define DRV_DUTY_Q15_ONE                            (32768U)
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

/* 系统时间、临界区、优先级和复位。 */
void drv_system_init(void);
uint32_t drv_time_now_ms(void);
void drv_time_tick_isr(void);
aurora_irq_state_t drv_irq_save(void);
void drv_irq_restore(aurora_irq_state_t state);
void drv_irq_configure_priorities(void);
DRV_SYSTEM_NORETURN void drv_system_reset(void);

/*
 * MCU上电供电资格。
 * PVD只用于“是否允许继续完整初始化”，不作为运行期弱光保护、故障锁存或系统复位源。
 */
bool drv_system_supply_qualifier_init(void);
bool drv_system_supply_monitor_ready(void);
bool drv_system_supply_is_good(void);
bool drv_system_wait_for_supply_stable(void);
void drv_system_supply_qualifier_stop(void);

/* ADC定时触发、DMA双半缓冲与中断应答。 */
bool drv_adc_init(void);
bool drv_adc_start(void);
const uint16_t *drv_adc_completed_block(uint8_t block_index);
size_t drv_adc_block_words(void);
uint8_t drv_adc_dma_irq_ack(void);

/* 单路异步Boost PWM与Break安全控制。 */
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

/* 内部OPA/COMP快速故障链。 */
bool drv_comp_init(void);
uint32_t drv_comp_fault_mask(void);
void drv_comp_irq_ack(void);

/* 继电器、Link和LED输出。 */
void drv_io_init(void);
void drv_io_set_relay(bool on);
void drv_io_set_link(bool on);
void drv_io_set_leds(bool run_on, bool fault_on);

/* 产品UART的环形发送与ISR字节接口。 */
bool drv_uart_init(void);
bool drv_uart_send(const uint8_t *data, size_t length);
bool drv_uart_tx_busy(void);
bool drv_uart_rx_ready_isr(void);
uint8_t drv_uart_read_isr(void);
void drv_uart_tx_isr(void);
void drv_uart_irq_ack(void);

/* 内部Flash读、页擦除和对齐编程。 */
bool drv_flash_read(uint32_t address, void *data, size_t length);
bool drv_flash_erase_page(uint32_t address);
bool drv_flash_program(uint32_t address, const void *data, size_t length);

/* 独立看门狗。只有Service健康监督允许调用feed。 */
bool drv_watchdog_init(uint32_t timeout_ms);
void drv_watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif
