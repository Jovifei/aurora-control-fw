#ifndef AURORA_DRV_FLASH_H
#define AURORA_DRV_FLASH_H

#include "driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 从内部Flash读取数据。 */
bool drv_flash_read(uint32_t address, void *data, size_t length);
/* 按芯片物理页粒度擦除Flash页。 */
bool drv_flash_erase_page(uint32_t address);
/* 按目标Flash编程约束写入数据。 */
bool drv_flash_program(uint32_t address, const void *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
