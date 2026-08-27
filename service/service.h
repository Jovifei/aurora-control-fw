#ifndef AURORA_SERVICE_H
#define AURORA_SERVICE_H

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    aurora_app_t app;
    volatile uint32_t safety_epoch;
    volatile uint32_t pending_fault_mask;
    volatile uint32_t event_flags;
    volatile uint32_t adc_timestamp_ms[2];
    uint32_t pwm_zero_sequence;
    uint32_t watchdog_seen;
    uint32_t watchdog_window_start_ms;
    uint32_t watchdog_started_ms;
    uint32_t last_telemetry_ms;
    uint32_t last_tick_poll_ms;
    volatile uint32_t adc_overrun_count;
    volatile uint32_t uart_rx_overrun_count;
    volatile uint8_t adc_completed_mask;
    volatile uint8_t adc_processing_mask;
    uint8_t pwm_arm_state;
    uint8_t uart_rx[256];
    volatile uint16_t uart_head;
    volatile uint16_t uart_tail;
    bool initialized;
} aurora_service_t;

bool aurora_service_init(aurora_service_t *service);
void aurora_service_poll(aurora_service_t *service);

/* ISR桥接：中断只搬运/发布事件，复杂业务全部在aurora_service_poll中完成。 */
void aurora_service_isr_tick(aurora_service_t *service);
void aurora_service_isr_adc_block(aurora_service_t *service, uint8_t block_index);
void aurora_service_isr_fast_fault(aurora_service_t *service, uint32_t fault_mask);
void aurora_service_isr_pwm_update(aurora_service_t *service);
void aurora_service_isr_uart_rx(aurora_service_t *service, uint8_t byte);

#ifdef __cplusplus
}
#endif

#endif
