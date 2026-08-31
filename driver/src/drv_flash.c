#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_flash.h"

/*---------------------------------------------------------------------------*
 * Name        : static bool range_in_nvm(uint32_t address, size_t length)
 * Input       : address - Flash地址；length - 数据长度
 * Output      : true表示请求范围完全位于双页NVM保留区
 * Description : 检查地址区间是否完整落在为双页参数Journal保留的内部Flash区域。
 *---------------------------------------------------------------------------*/
static bool range_in_nvm(uint32_t address, size_t length)
{
    const uint32_t first = BOARD_FLASH_PAGE_A_ADDRESS;
    const uint32_t last = BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE;
    return (address >= first) && ((uint64_t)address + length <= last);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_read(uint32_t address, void *data, size_t length)
 * Input       : address - Flash地址；data - 数据缓冲区；length - 数据长度
 * Output      : true表示指定范围读取成功；false表示参数或范围无效
 * Description : 在NVM保留区内按字节读取内部Flash；越界或空指针立即拒绝。
 *---------------------------------------------------------------------------*/
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

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_erase_page(uint32_t address)
 * Input       : address - Flash地址
 * Output      : true表示目标页擦除成功；false表示地址、对齐或DDL操作失败
 * Description : 仅在地址为A/B页且PWM未输出时擦除一个参数页，并在操作前后管理Flash解锁。
 *---------------------------------------------------------------------------*/
bool drv_flash_erase_page(uint32_t address)
{
    ErrorStatus status;

    if (((address != BOARD_FLASH_PAGE_A_ADDRESS) && (address != BOARD_FLASH_PAGE_B_ADDRESS)) ||
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

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_program(uint32_t address, const void *data, size_t length)
 * Input       : address - Flash地址；data - 数据缓冲区；length - 数据长度
 * Output      : true表示全部字编程成功；false表示参数、对齐、范围或DDL操作失败
 * Description : 仅在地址/长度4字节对齐、范围合法且PWM关闭时编程内部Flash。
 *---------------------------------------------------------------------------*/
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
