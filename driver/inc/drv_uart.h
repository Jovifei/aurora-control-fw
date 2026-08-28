#ifndef AURORA_DRV_UART_H
#define AURORA_DRV_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool drv_uart_init(void);
bool drv_uart_send(const uint8_t *data, size_t length);
bool drv_uart_tx_busy(void);
bool drv_uart_rx_ready_isr(void);
uint8_t drv_uart_read_isr(void);
void drv_uart_tx_isr(void);
void drv_uart_irq_ack(void);

#endif
