#ifndef AURORA_MOCK_DRIVER_H
#define AURORA_MOCK_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mock_reset(void);
void mock_advance_ms(uint32_t ms);
void mock_set_break(bool active);
void mock_apply_uev(void);
uint16_t mock_duty(void);
bool mock_pwm_active(void);
uint32_t mock_watchdog_feeds(void);
size_t mock_uart_tx_length(void);
const uint8_t *mock_uart_tx_data(void);
void mock_uart_clear_tx(void);
bool mock_relay(void);
uint16_t *mock_adc_block(uint8_t index);

#endif
