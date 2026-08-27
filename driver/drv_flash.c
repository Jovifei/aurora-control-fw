#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_flash.h"


static bool range_in_nvm(uint32_t address, size_t length)
{
    const uint32_t first = BOARD_FLASH_PAGE_A_ADDRESS;
    const uint32_t last = BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE;
    return (address >= first) && ((uint64_t)address + length <= last);
}

bool drv_flash_read(uint32_t address, void *data, size_t length)
{
    if ((data == NULL) || !range_in_nvm(address, length))
    {
        return false;
    }
    {
        size_t i;
        uint8_t *dst = (uint8_t *)data;
        const uint8_t *src = (const uint8_t *)(uintptr_t)address;
        for (i = 0U; i < length; ++i)
        {
            dst[i] = src[i];
        }
    }
    return true;
}

bool drv_flash_erase_page(uint32_t address)
{
    ErrorStatus status;

    if (((address != BOARD_FLASH_PAGE_A_ADDRESS) &&
         (address != BOARD_FLASH_PAGE_B_ADDRESS)) ||
        drv_pwm_output_active())
    {
        return false;
    }

    DDL_FLASH_RKEY_Unlock();
    DDL_FLASH_MKEY_Unlock();
    status = DDL_FLASH_EraseSector(address);
    DDL_FLASH_MKEY_Lock();
    DDL_FLASH_RKEY_Lock();
    return status == SUCCESS;
}

bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    ErrorStatus status;

    if ((data == NULL) || ((address & 3U) != 0U) || ((length & 3U) != 0U) ||
        !range_in_nvm(address, length) || drv_pwm_output_active())
    {
        return false;
    }

    DDL_FLASH_RKEY_Unlock();
    DDL_FLASH_MKEY_Unlock();
    status = DDL_FLASH_Write(address, (uint32_t)length, (uint8_t *)(uintptr_t)data);
    DDL_FLASH_MKEY_Lock();
    DDL_FLASH_RKEY_Lock();
    return status == SUCCESS;
}
