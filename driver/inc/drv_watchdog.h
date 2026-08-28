#ifndef AURORA_DRV_WATCHDOG_H
#define AURORA_DRV_WATCHDOG_H

#include <stdbool.h>
#include <stdint.h>

bool drv_watchdog_init(uint32_t timeout_ms);
void drv_watchdog_feed(void);

#endif
