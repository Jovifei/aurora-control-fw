#ifndef AURORA_DRV_UART_H
#define AURORA_DRV_UART_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化蓝牙/Debug共享USART及当前PinMap路由。 */
bool drv_uart_init(void);
/* 非阻塞写入TX环形缓冲。 */
bool drv_uart_send(const uint8_t *data, size_t length);
/* 查询TX环形缓冲是否忙。 */
bool drv_uart_tx_busy(void);
/* 在USART ISR中查询RX是否就绪。 */
bool drv_uart_rx_ready_isr(void);
/* 在USART ISR中读取一个RX字节。 */
uint8_t drv_uart_read_isr(void);
/* 在USART ISR中推进TX发送。 */
void drv_uart_tx_isr(void);
/* 应答USART错误和中断标志。 */
void drv_uart_irq_ack(void);

#ifdef __cplusplus
}
#endif

#endif
