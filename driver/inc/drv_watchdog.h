#ifndef AURORA_DRV_WATCHDOG_H
#define AURORA_DRV_WATCHDOG_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化目标独立看门狗。 */
bool drv_watchdog_init(uint32_t timeout_ms);
/* 仅由应用健康监督在票据齐全时调用。 */
void drv_watchdog_feed(void);

#ifdef __cplusplus
}
#endif

#endif
