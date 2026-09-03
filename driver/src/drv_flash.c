#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_flash.h"

/*---------------------------------------------------------------------------*
 * Name        : static bool range_in_nvm(uint32_t address, size_t length)
 * Input       : address - Flash地址；length - 数据长度
 * Output      : true表示请求范围完全位于双页NVM保留区
 * Description : 使用64位端地址检查，地址0和整数回绕都不能绕过0xFC00~0xFFFF边界。
 *---------------------------------------------------------------------------*/
static bool range_in_nvm(uint32_t address, size_t length)
{
    const uint32_t first = BOARD_FLASH_PAGE_A_ADDRESS;
    const uint32_t last = BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE;
    const uint64_t end = (uint64_t)address + (uint64_t)length;

    return (length != 0U) && (address >= first) && (end <= (uint64_t)last);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool range_in_single_nvm_page(uint32_t address, size_t length)
 * Input       : address - Flash地址；length - 数据长度
 * Output      : true表示请求完整落在A页或B页之一
 * Description : 编程操作禁止一次跨越两个Journal页，避免单个长度/偏移Bug同时破坏A/B冗余。
 *---------------------------------------------------------------------------*/
static bool range_in_single_nvm_page(uint32_t address, size_t length)
{
    const uint64_t end = (uint64_t)address + (uint64_t)length;
    const uint64_t a_first = BOARD_FLASH_PAGE_A_ADDRESS;
    const uint64_t a_last = a_first + BOARD_FLASH_PAGE_SIZE;
    const uint64_t b_first = BOARD_FLASH_PAGE_B_ADDRESS;
    const uint64_t b_last = b_first + BOARD_FLASH_PAGE_SIZE;

    if (length == 0U)
    {
        return false;
    }
    return (((uint64_t)address >= a_first) && (end <= a_last)) ||
           (((uint64_t)address >= b_first) && (end <= b_last));
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
 * Description : 仅允许A/B页、PWM关闭且VDD资格有效时擦除；欠压只拒绝写，不触发掉电保存。
 *---------------------------------------------------------------------------*/
bool drv_flash_erase_page(uint32_t address)
{
    ErrorStatus status;

    if (((address != BOARD_FLASH_PAGE_A_ADDRESS) && (address != BOARD_FLASH_PAGE_B_ADDRESS)) ||
        drv_pwm_output_active() || !drv_system_flash_supply_is_safe())
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
 * Description : 仅在单页范围、4字节对齐、PWM关闭且VDD资格有效时编程，禁止地址0和跨A/B页写。
 *---------------------------------------------------------------------------*/
bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    ErrorStatus status = SUCCESS;
    size_t offset;

    if ((data == NULL) || ((address & 3U) != 0U) || ((length & 3U) != 0U) ||
        !range_in_single_nvm_page(address, length) || drv_pwm_output_active() ||
        !drv_system_flash_supply_is_safe())
    {
        return false;
    }

    DDL_FLASH_RKEY_Unlock();
    DDL_FLASH_MKEY_Unlock();
    for (offset = 0U; offset < length; offset += sizeof(uint32_t))
    {
        /* 每个32-bit word前重新检查PVD，欠压后不再启动下一次Flash编程。 */
        if (!drv_system_flash_supply_is_safe())
        {
            status = ERROR;
            break;
        }
        status = DDL_FLASH_Write(address + (uint32_t)offset, (uint32_t)sizeof(uint32_t),
                                 (uint8_t *)(uintptr_t)((const uint8_t *)data + offset));
        if (status != SUCCESS)
        {
            break;
        }
    }
    DDL_FLASH_MKEY_Lock();
    DDL_FLASH_RKEY_Lock();
    return status == SUCCESS;
}
