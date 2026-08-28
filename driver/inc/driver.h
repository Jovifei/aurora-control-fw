#ifndef AURORA_DRIVER_H
#define AURORA_DRIVER_H

/*
 * Driver统一入口：应用层只依赖这些硬件无关契约。
 * 各drv_*.h不得包含APP业务头；driver/src允许调用vendor芯片库。
 */
#include "drv_adc.h"
#include "drv_board.h"
#include "drv_comp.h"
#include "drv_flash.h"
#include "drv_io.h"
#include "drv_pwm.h"
#include "drv_system.h"
#include "drv_uart.h"
#include "drv_watchdog.h"

#endif
