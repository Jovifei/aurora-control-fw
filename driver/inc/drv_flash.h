#ifndef AURORA_DRV_FLASH_H
#define AURORA_DRV_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool drv_flash_read(uint32_t address, void *data, size_t length);
bool drv_flash_erase_page(uint32_t address);
bool drv_flash_program(uint32_t address, const void *data, size_t length);

#endif
