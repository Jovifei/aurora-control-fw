#ifndef AURORA_DRV_IO_H
#define AURORA_DRV_IO_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化继电器、Link和LED GPIO安全态。 */
void drv_io_init(void);
/* 设置继电器输出。 */
void drv_io_set_relay(bool on);
/* 设置Link预留输出。 */
void drv_io_set_link(bool on);
/* 设置运行灯和故障灯。 */
void drv_io_set_leds(bool run_on, bool fault_on);

#ifdef __cplusplus
}
#endif

#endif
