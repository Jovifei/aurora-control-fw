#ifndef AURORA_DRV_IO_H
#define AURORA_DRV_IO_H

#include <stdbool.h>

void drv_io_init(void);
void drv_io_set_relay(bool on);
void drv_io_set_link(bool on);
void drv_io_set_leds(bool run_on, bool fault_on);

#endif
