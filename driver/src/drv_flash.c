#include "driver.h"

#include "board_config.h"
#include "g32f031_ddl_flash.h"

#if BOARD_FLASH_PAGE_SIZE == 0U
#error "BOARD_FLASH_PAGE_SIZE must be non-zero"
#endif
#if (BOARD_FLASH_PAGE_A_ADDRESS % BOARD_FLASH_PAGE_SIZE) != 0U
#error "Flash Journal page A must be physically page aligned"
#endif
#if (BOARD_FLASH_PAGE_B_ADDRESS % BOARD_FLASH_PAGE_SIZE) != 0U
#error "Flash Journal page B must be physically page aligned"
#endif
#if BOARD_FLASH_PAGE_B_ADDRESS != (BOARD_FLASH_PAGE_A_ADDRESS + BOARD_FLASH_PAGE_SIZE)
#error "Flash Journal pages must be adjacent"
#endif
#if (BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE) != BOARD_FLASH_TOTAL_SIZE_BYTES
#error "Flash Journal must occupy the final two physical pages only"
#endif

/*---------------------------------------------------------------------------*
 * Name        : static bool range_in_nvm(uint32_t address, size_t length)
 * Input       : address - Flash地址；length - 数据长度
 * Output      : true表示请求范围完全位于双页NVM保留区
 * Description : 检查地址区间是否完整落在最后1KiB参数区；0长度和地址溢出均拒绝。
 *---------------------------------------------------------------------------*/
static bool range_in_nvm(uint32_t address, size_t length)
{
    const uint32_t first = BOARD_FLASH_PAGE_A_ADDRESS;
    const uint32_t last = BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE;
    const uint64_t range_end = (uint64_t)address + (uint64_t)length;

    return (length != 0U) && (address >= first) && (range_end <= (uint64_t)last);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool range_in_single_nvm_page(uint32_t address, size_t length)
 * Input       : address - Flash地址；length - 数据长度
 * Output      : true表示编程范围完整位于A页或B页中的单独一页
 * Description : 即使调用方地址合法，也禁止一次编程跨越A/B物理页边界，保护双页Journal隔离性。
 *---------------------------------------------------------------------------*/
static bool range_in_single_nvm_page(uint32_t address, size_t length)
{
    const uint64_t range_end = (uint64_t)address + (uint64_t)length;
    const uint64_t page_a_end = (uint64_t)BOARD_FLASH_PAGE_A_ADDRESS + BOARD_FLASH_PAGE_SIZE;
    const uint64_t page_b_end = (uint64_t)BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE;

    if (!range_in_nvm(address, length))
    {
        return false;
    }
    if ((address >= BOARD_FLASH_PAGE_A_ADDRESS) && (range_end <= page_a_end))
    {
        return true;
    }
    return (address >= BOARD_FLASH_PAGE_B_ADDRESS) && (range_end <= page_b_end);
}

/*---------------------------------------------------------------------------*
 * Name        : bool drv_flash_read(uint32_t address, void *data, size_t length)
 * Input       : address - Flash地址；data - 数据缓冲区；length - 数据长度
 * Output      : true表示指定范围读取成功；false表示参数或范围无效
 * Description : 仅允许读取双页参数保留区；0长度、越界或空指针立即拒绝。
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
 * Description : 只允许精确擦A/B页首地址；PWM输出期间绝不擦写。
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
 * Description : 仅允许4字节对齐且完全位于单个Journal物理页中的非零长度编程；禁止跨页和功率运行写入。
 *---------------------------------------------------------------------------*/
bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    ErrorStatus status;

    if ((data == NULL) || (length == 0U) || ((address & 3U) != 0U) || ((length & 3U) != 0U) ||
        !range_in_single_nvm_page(address, length) || drv_pwm_output_active())
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
