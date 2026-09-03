#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8", newline="\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)


# -----------------------------------------------------------------------------
# 1. Board/Driver Flash hard bounds: reserved last 1 KiB only, single-page writes.
# -----------------------------------------------------------------------------
p = read("driver/inc/board_config.h")
p = replace_once(
    p,
    "/* G32F031内部Flash物理擦除页大小，字节。 */\n#define BOARD_FLASH_PAGE_SIZE                       (512UL)\n",
    "/* G32F031K8T内部Flash总容量，字节。 */\n"
    "#define BOARD_FLASH_TOTAL_SIZE_BYTES                (64UL * 1024UL)\n"
    "/* G32F031内部Flash物理擦除页大小，字节。 */\n"
    "#define BOARD_FLASH_PAGE_SIZE                       (512UL)\n",
    "board total flash size",
)
write("driver/inc/board_config.h", p)

write(
    "driver/src/drv_flash.c",
    r'''#include "driver.h"

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
''',
)

# -----------------------------------------------------------------------------
# 2. Journal compile-time layout guard + runtime retry/active-page state.
# -----------------------------------------------------------------------------
p = read("app/inc/storage.h")
p = replace_once(
    p,
    "#define AURORA_STORAGE_CHARGE_HISTORY_OFFSET        \\\n    (AURORA_STORAGE_ENERGY_HISTORY_OFFSET + \\\n     (AURORA_ENERGY_HISTORY_POINT_COUNT * 4U))\n",
    "#define AURORA_STORAGE_CHARGE_HISTORY_OFFSET        \\\n    (AURORA_STORAGE_ENERGY_HISTORY_OFFSET + \\\n     (AURORA_ENERGY_HISTORY_POINT_COUNT * 4U))\n"
    "/* 当前页实际编码总长度。 */\n"
    "#define AURORA_STORAGE_ENCODED_SIZE                 \\\n    (AURORA_STORAGE_HEADER_SIZE + AURORA_STORAGE_PAYLOAD_SIZE)\n"
    "/* v3载荷最后一个数组的结束偏移，用于编译期验证地址表与PAYLOAD_SIZE一致。 */\n"
    "#define AURORA_STORAGE_PAYLOAD_USED_SIZE            \\\n    (AURORA_STORAGE_CHARGE_HISTORY_OFFSET + (AURORA_ENERGY_HISTORY_POINT_COUNT * 4U))\n\n"
    "#if AURORA_STORAGE_PAYLOAD_USED_SIZE != AURORA_STORAGE_PAYLOAD_SIZE\n"
    "#error \"Flash v3 payload layout does not match AURORA_STORAGE_PAYLOAD_SIZE\"\n"
    "#endif\n"
    "#if AURORA_STORAGE_ENCODED_SIZE > AURORA_STORAGE_PAGE_SIZE\n"
    "#error \"Flash v3 encoded record exceeds one physical page\"\n"
    "#endif\n"
    "#if (AURORA_STORAGE_ENCODED_SIZE & 3U) != 0U\n"
    "#error \"Flash v3 encoded record must remain 4-byte aligned\"\n"
    "#endif\n"
    "#if (AURORA_STORAGE_COMMIT_OFFSET + 4U) > AURORA_STORAGE_HEADER_SIZE\n"
    "#error \"Flash v3 Commit Marker must remain inside the fixed header\"\n"
    "#endif\n",
    "storage layout guards",
)
p = replace_once(
    p,
    "/* Flash双页Journal运行状态。 */\ntypedef struct\n{\n",
    "/* 当前最后有效记录所在物理页；不依赖sequence奇偶推断，避免异常恢复时擦掉唯一好页。 */\n"
    "#define AURORA_STORAGE_ACTIVE_NONE                  (0U)\n"
    "#define AURORA_STORAGE_ACTIVE_PAGE_A                (1U)\n"
    "#define AURORA_STORAGE_ACTIVE_PAGE_B                (2U)\n\n"
    "/* Flash双页Journal运行状态。 */\ntypedef struct\n{\n",
    "storage active page constants",
)
p = replace_once(
    p,
    "    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */\n"
    "    bool repair_pending;                             /* true表示另一页需在安全窗口重建冗余。 */\n"
    "    aurora_storage_page_status_t page_a_status;      /* 最近启动读取A页分类。 */\n"
    "    aurora_storage_page_status_t page_b_status;      /* 最近启动读取B页分类。 */\n",
    "    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */\n"
    "    bool repair_pending;                             /* true表示另一页需在安全窗口重建冗余。 */\n"
    "    bool write_blocked;                              /* 连续写失败超过一次重试后，本次上电禁止继续擦写。 */\n"
    "    uint8_t write_failure_count;                     /* 当前上电连续Flash事务失败次数。 */\n"
    "    uint8_t active_page;                             /* AURORA_STORAGE_ACTIVE_*，表示最后有效页。 */\n"
    "    aurora_storage_page_status_t page_a_status;      /* 最近启动读取A页分类。 */\n"
    "    aurora_storage_page_status_t page_b_status;      /* 最近启动读取B页分类。 */\n",
    "storage runtime fields",
)
write("app/inc/storage.h", p)

p = read("app/inc/app_config.h")
p = replace_once(
    p,
    "#define AURORA_STORAGE_DIRTY_HOLD_MS                (1000U)\n",
    "#define AURORA_STORAGE_DIRTY_HOLD_MS                (1000U)\n"
    "/* 首次Flash事务失败后最多再重试一次；再次失败则本次上电禁止继续擦写。 */\n"
    "#define AURORA_STORAGE_WRITE_RETRY_MAX              (1U)\n",
    "storage retry config",
)
write("app/inc/app_config.h", p)

# -----------------------------------------------------------------------------
# 3. Runtime: only stopped states write; preserve last-good page and sequence;
#    commit-last + readback; bounded retry. Never use runtime PVD/LVD to write.
# -----------------------------------------------------------------------------
p = read("app/src/main.c")
old_runtime_storage = r'''/*---------------------------------------------------------------------------*
 * Name        : static void runtime_storage(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 只有PWM关闭且物理继电器断开时执行双页Journal保存，Commit Marker最后写入。
 *---------------------------------------------------------------------------*/
static void runtime_storage(aurora_runtime_t *runtime, uint32_t now_ms)
{
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t target;
    size_t used;

    if (!runtime->app.storage.dirty ||
        ((now_ms - runtime->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        drv_pwm_output_active() || runtime->relay_applied)
    {
        return;
    }

    runtime->app.storage.sequence++;
    target = ((runtime->app.storage.sequence & 1U) != 0U) ? drv_board_flash_page_a()
                                                          : drv_board_flash_page_b();
    used = aurora_storage_encode_page(&runtime->app.storage, page, sizeof(page), false);
    if ((used < AURORA_STORAGE_HEADER_SIZE) || !drv_flash_erase_page(target) ||
        !drv_flash_program(target, page, AURORA_STORAGE_COMMIT_OFFSET) ||
        !drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t),
                           &page[AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t)],
                           used - AURORA_STORAGE_COMMIT_OFFSET - sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
        return;
    }

    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET,
                           &((uint32_t){AURORA_STORAGE_COMMIT_MARKER}), sizeof(uint32_t)))
    {
        aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
        return;
    }
    runtime->app.storage.dirty = false;
    runtime->app.storage.repair_pending = false;
    if (target == drv_board_flash_page_a())
    {
        runtime->app.storage.page_a_status = AURORA_STORAGE_PAGE_VALID;
    }
    else
    {
        runtime->app.storage.page_b_status = AURORA_STORAGE_PAGE_VALID;
    }
}
'''
new_runtime_storage = r'''/*---------------------------------------------------------------------------*
 * Name        : static bool runtime_storage_stop_state(aurora_power_state_t state)
 * Input       : state - 当前功率级状态
 * Output      : true表示当前属于明确停机/等待状态，可考虑写Flash
 * Description : Flash不在欠压/PVD事件里抢写；只在WAIT_PV/NO_SUN/FAULT/OFF且物理PWM、Relay均关闭时保存。
 *---------------------------------------------------------------------------*/
static bool runtime_storage_stop_state(aurora_power_state_t state)
{
    return (state == AURORA_POWER_WAIT_PV) || (state == AURORA_POWER_NO_SUN) ||
           (state == AURORA_POWER_FAULT) || (state == AURORA_POWER_OFF);
}

/*---------------------------------------------------------------------------*
 * Name        : static void runtime_storage_record_failure(
 *               aurora_runtime_t *runtime, uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 保留dirty和最后有效sequence；首次失败等待1s后仅重试一次，再失败则本次上电禁止继续擦写。
 *---------------------------------------------------------------------------*/
static void runtime_storage_record_failure(aurora_runtime_t *runtime, uint32_t now_ms)
{
    if (runtime->app.storage.write_failure_count < 255U)
    {
        runtime->app.storage.write_failure_count++;
    }
    runtime->app.storage.dirty_since_ms = now_ms;
    if (runtime->app.storage.write_failure_count > AURORA_STORAGE_WRITE_RETRY_MAX)
    {
        runtime->app.storage.write_blocked = true;
    }
    aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
}

/*---------------------------------------------------------------------------*
 * Name        : static bool runtime_storage_program_page(uint32_t target,
 *               uint8_t *page, size_t used)
 * Input       : target - A/B目标页；page - 未提交页镜像；used - 有效编码长度
 * Output      : true表示擦除、数据、Commit Marker和逐字回读均成功
 * Description : Commit Marker最后落盘；完成后逐4字节回读，任何异常都不允许上层推进active page/sequence。
 *---------------------------------------------------------------------------*/
static bool runtime_storage_program_page(uint32_t target, uint8_t *page, size_t used)
{
    uint8_t verify[sizeof(uint32_t)];
    size_t offset;
    const uint32_t commit = AURORA_STORAGE_COMMIT_MARKER;

    if ((page == NULL) || (used != AURORA_STORAGE_ENCODED_SIZE) ||
        !drv_flash_erase_page(target) ||
        !drv_flash_program(target, page, AURORA_STORAGE_COMMIT_OFFSET) ||
        !drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t),
                           &page[AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t)],
                           used - AURORA_STORAGE_COMMIT_OFFSET - sizeof(uint32_t)) ||
        !drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET, &commit, sizeof(commit)))
    {
        return false;
    }

    page[AURORA_STORAGE_COMMIT_OFFSET + 0U] = (uint8_t)commit;
    page[AURORA_STORAGE_COMMIT_OFFSET + 1U] = (uint8_t)(commit >> 8U);
    page[AURORA_STORAGE_COMMIT_OFFSET + 2U] = (uint8_t)(commit >> 16U);
    page[AURORA_STORAGE_COMMIT_OFFSET + 3U] = (uint8_t)(commit >> 24U);

    for (offset = 0U; offset < used; offset += sizeof(uint32_t))
    {
        if (!drv_flash_read(target + (uint32_t)offset, verify, sizeof(verify)) ||
            (memcmp(verify, &page[offset], sizeof(verify)) != 0))
        {
            return false;
        }
    }
    return true;
}

/*---------------------------------------------------------------------------*
 * Name        : static void runtime_storage(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 只在明确停机态保存双页Journal；失败不推进sequence、不切换目标页，保住上一份有效页并限制重试次数。
 *---------------------------------------------------------------------------*/
static void runtime_storage(aurora_runtime_t *runtime, uint32_t now_ms)
{
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t target;
    uint32_t previous_sequence;
    uint32_t next_sequence;
    uint8_t next_active_page;
    size_t used;

    if (!runtime->app.storage.dirty || runtime->app.storage.write_blocked ||
        ((now_ms - runtime->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        !runtime_storage_stop_state(runtime->app.power_stage.state) || drv_pwm_output_active() ||
        runtime->relay_applied)
    {
        return;
    }

    previous_sequence = runtime->app.storage.sequence;
    next_sequence = previous_sequence + 1U;
    if (runtime->app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A)
    {
        target = drv_board_flash_page_b();
        next_active_page = AURORA_STORAGE_ACTIVE_PAGE_B;
    }
    else
    {
        target = drv_board_flash_page_a();
        next_active_page = AURORA_STORAGE_ACTIVE_PAGE_A;
    }

    /* encode需要把待提交sequence写进页头，但RAM中的last-good sequence只有事务成功后才能推进。 */
    runtime->app.storage.sequence = next_sequence;
    used = aurora_storage_encode_page(&runtime->app.storage, page, sizeof(page), false);
    runtime->app.storage.sequence = previous_sequence;

    if ((used != AURORA_STORAGE_ENCODED_SIZE) ||
        !runtime_storage_program_page(target, page, used))
    {
        runtime_storage_record_failure(runtime, now_ms);
        return;
    }

    runtime->app.storage.sequence = next_sequence;
    runtime->app.storage.active_page = next_active_page;
    runtime->app.storage.dirty = false;
    runtime->app.storage.repair_pending = false;
    runtime->app.storage.write_failure_count = 0U;
    runtime->app.storage.write_blocked = false;
    if (target == drv_board_flash_page_a())
    {
        runtime->app.storage.page_a_status = AURORA_STORAGE_PAGE_VALID;
    }
    else
    {
        runtime->app.storage.page_b_status = AURORA_STORAGE_PAGE_VALID;
    }
}
'''
p = replace_once(p, old_runtime_storage, new_runtime_storage, "runtime storage block")

# Track which physical page supplied the last-good record. This removes any sequence-parity assumption.
p = replace_once(
    p,
    "        if (choose_b)\n        {\n            runtime->app.storage.settings = settings_b;\n            runtime->app.storage.sequence = seq_b;\n        }\n        else\n        {\n            runtime->app.storage.settings = settings_a;\n            runtime->app.storage.sequence = seq_a;\n        }\n",
    "        if (choose_b)\n        {\n            runtime->app.storage.settings = settings_b;\n            runtime->app.storage.sequence = seq_b;\n            runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_PAGE_B;\n        }\n        else\n        {\n            runtime->app.storage.settings = settings_a;\n            runtime->app.storage.sequence = seq_a;\n            runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_PAGE_A;\n        }\n",
    "load storage active page",
)
p = replace_once(
    p,
    "    if ((status_a == AURORA_STORAGE_PAGE_ERASED) && (status_b == AURORA_STORAGE_PAGE_ERASED))\n    {\n        aurora_storage_mark_dirty(&runtime->app.storage, now_ms);\n        return;\n    }\n",
    "    if ((status_a == AURORA_STORAGE_PAGE_ERASED) && (status_b == AURORA_STORAGE_PAGE_ERASED))\n    {\n        runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_NONE;\n        aurora_storage_mark_dirty(&runtime->app.storage, now_ms);\n        return;\n    }\n",
    "empty journal active page",
)
write("app/src/main.c", p)

# -----------------------------------------------------------------------------
# 4. Host mock: mirror target boundaries + failure injection/counters.
# -----------------------------------------------------------------------------
p = read("tests/mock_driver.h")
p = replace_once(
    p,
    "bool mock_relay(void);\nuint16_t *mock_adc_block(uint8_t index);\n",
    "bool mock_relay(void);\n"
    "uint16_t *mock_adc_block(uint8_t index);\n"
    "void mock_fail_next_flash_program(void);\n"
    "uint32_t mock_flash_erase_count(void);\n"
    "uint32_t mock_flash_program_count(void);\n",
    "mock flash declarations",
)
write("tests/mock_driver.h", p)

p = read("tests/mock_driver.c")
p = replace_once(
    p,
    "static uint8_t g_flash[MOCK_FLASH_SIZE_BYTES];\n",
    "static uint8_t g_flash[MOCK_FLASH_SIZE_BYTES];\n"
    "static uint32_t g_flash_erase_count;\n"
    "static uint32_t g_flash_program_count;\n"
    "static bool g_fail_next_flash_program;\n",
    "mock flash globals",
)
p = replace_once(
    p,
    "    if ((address < MOCK_FLASH_BASE_ADDRESS) ||\n        (range_end > (uint64_t)MOCK_FLASH_END_ADDRESS)) {\n",
    "    if ((length == 0U) || (address < MOCK_FLASH_BASE_ADDRESS) ||\n        (range_end > (uint64_t)MOCK_FLASH_END_ADDRESS)) {\n",
    "mock zero length range",
)
p = replace_once(
    p,
    "    g_uart_tx_length = 0U;\n    memset(g_flash, 0xFF, sizeof(g_flash));\n",
    "    g_uart_tx_length = 0U;\n"
    "    memset(g_flash, 0xFF, sizeof(g_flash));\n"
    "    g_flash_erase_count = 0U;\n"
    "    g_flash_program_count = 0U;\n"
    "    g_fail_next_flash_program = false;\n",
    "mock reset flash state",
)
# Add accessors before drv_system_init.
anchor = "/*---------------------------------------------------------------------------*\n * Name        : void drv_system_init(void)"
insert = r'''/*---------------------------------------------------------------------------*
 * Name        : void mock_fail_next_flash_program(void)
 * Input       : 无
 * Output      : 无
 * Description : 让下一次合法Flash编程尝试失败且不修改模拟Flash，用于掉电/DDL失败回归。
 *---------------------------------------------------------------------------*/
void mock_fail_next_flash_program(void)
{
    g_fail_next_flash_program = true;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t mock_flash_erase_count(void)
 * Input       : 无
 * Output      : 合法Flash擦除尝试次数
 * Description : 用于验证运行态不写Flash以及失败后只允许一次重试。
 *---------------------------------------------------------------------------*/
uint32_t mock_flash_erase_count(void)
{
    return g_flash_erase_count;
}

/*---------------------------------------------------------------------------*
 * Name        : uint32_t mock_flash_program_count(void)
 * Input       : 无
 * Output      : 合法Flash编程尝试次数
 * Description : 用于验证事务阶段数和失败后禁止无限重复编程。
 *---------------------------------------------------------------------------*/
uint32_t mock_flash_program_count(void)
{
    return g_flash_program_count;
}

'''
if anchor not in p:
    raise SystemExit("mock accessor anchor missing")
p = p.replace(anchor, insert + anchor, 1)
# Erase/program implementations.
p = replace_once(
    p,
    "    memset(&g_flash[offset], 0xFF, MOCK_FLASH_PAGE_SIZE_BYTES);\n    return true;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_program",
    "    g_flash_erase_count++;\n"
    "    memset(&g_flash[offset], 0xFF, MOCK_FLASH_PAGE_SIZE_BYTES);\n"
    "    return true;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_program",
    "mock erase counter",
)
old_prog = r'''bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    size_t offset;
    size_t i;

    if ((data == NULL) || g_pwm_active || !flash_range(address, length, &offset)) {
        return false;
    }

    for (i = 0U; i < length; ++i) {
        g_flash[offset + i] &= ((const uint8_t *)data)[i];
    }

    return true;
}
'''
new_prog = r'''bool drv_flash_program(uint32_t address, const void *data, size_t length)
{
    size_t offset;
    size_t i;
    const uint64_t range_end = (uint64_t)address + (uint64_t)length;
    const uint64_t page_a_end = (uint64_t)MOCK_FLASH_BASE_ADDRESS + MOCK_FLASH_PAGE_SIZE_BYTES;
    const uint64_t page_b_start = page_a_end;

    if ((data == NULL) || g_pwm_active || ((address & 3U) != 0U) || ((length & 3U) != 0U) ||
        !flash_range(address, length, &offset) ||
        !(((address >= MOCK_FLASH_BASE_ADDRESS) && (range_end <= page_a_end)) ||
          ((address >= page_b_start) && (range_end <= MOCK_FLASH_END_ADDRESS)))) {
        return false;
    }

    g_flash_program_count++;
    if (g_fail_next_flash_program) {
        g_fail_next_flash_program = false;
        return false;
    }

    for (i = 0U; i < length; ++i) {
        g_flash[offset + i] &= ((const uint8_t *)data)[i];
    }

    return true;
}
'''
p = replace_once(p, old_prog, new_prog, "mock program implementation")
write("tests/mock_driver.c", p)

# -----------------------------------------------------------------------------
# 5. Behavioral regression tests.
# -----------------------------------------------------------------------------
p = read("tests/test_v0103.c")
insert_tests = r'''
/*---------------------------------------------------------------------------*
 * Name        : static void test_storage_driver_rejects_out_of_bounds_and_cross_page(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证0地址、0长度和跨A/B页编程都被Driver拒绝，不能复现120W active-address写向量表失效模式。
 *---------------------------------------------------------------------------*/
static void test_storage_driver_rejects_out_of_bounds_and_cross_page(void)
{
    uint32_t words[2] = {0x12345678UL, 0xA5A5A5A5UL};
    mock_reset();
    CHECK(!drv_flash_program(0x00000000UL, words, sizeof(words)));
    CHECK(!drv_flash_program(drv_board_flash_page_a(), words, 0U));
    CHECK(!drv_flash_program(drv_board_flash_page_a() + AURORA_STORAGE_PAGE_SIZE - 4U,
                             words, sizeof(words)));
    CHECK(mock_flash_program_count() == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_storage_writes_only_in_stopped_states(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证即使dirty且PWM/Relay均关闭，PRECHARGE等非停机状态也不能写Flash；WAIT_PV才允许事务落盘。
 *---------------------------------------------------------------------------*/
static void test_storage_writes_only_in_stopped_states(void)
{
    aurora_runtime_t runtime;

    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    CHECK(runtime.app.storage.dirty);
    runtime.app.power_stage.state = AURORA_POWER_PRECHARGE;
    runtime.app.power_command.state = AURORA_POWER_PRECHARGE;
    mock_advance_ms(AURORA_STORAGE_DIRTY_HOLD_MS + 1U);
    aurora_runtime_poll(&runtime);
    CHECK(mock_flash_erase_count() == 0U);
    CHECK(mock_flash_program_count() == 0U);
    CHECK(runtime.app.storage.dirty);

    runtime.app.power_stage.state = AURORA_POWER_WAIT_PV;
    runtime.app.power_command.state = AURORA_POWER_WAIT_PV;
    aurora_runtime_poll(&runtime);
    CHECK(mock_flash_erase_count() == 1U);
    CHECK(mock_flash_program_count() == 3U);
    CHECK(!runtime.app.storage.dirty);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_storage_failed_write_preserves_last_good_and_bounds_retry(void)
 * Input       : 无
 * Output      : 无
 * Description : 验证Flash失败不会提前推进sequence/切换active页；只重试一次，连续失败后禁止本次上电继续擦写。
 *---------------------------------------------------------------------------*/
static void test_storage_failed_write_preserves_last_good_and_bounds_retry(void)
{
    aurora_runtime_t runtime;
    aurora_persistent_settings_t decoded;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    uint32_t decoded_sequence = 0U;
    uint32_t erase_before_blocked_poll;
    uint32_t program_before_blocked_poll;

    mock_reset();
    CHECK(aurora_runtime_init(&runtime));
    runtime.app.power_stage.state = AURORA_POWER_WAIT_PV;
    runtime.app.power_command.state = AURORA_POWER_WAIT_PV;
    mock_advance_ms(AURORA_STORAGE_DIRTY_HOLD_MS + 1U);
    aurora_runtime_poll(&runtime);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    CHECK(aurora_storage_classify_page(page, sizeof(page), &decoded, &decoded_sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(decoded_sequence == 2U);

    runtime.app.storage.settings.settings_revision++;
    aurora_storage_mark_dirty(&runtime.app.storage, drv_time_now_ms());
    mock_fail_next_flash_program();
    mock_advance_ms(AURORA_STORAGE_DIRTY_HOLD_MS + 1U);
    aurora_runtime_poll(&runtime);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.write_failure_count == 1U);
    CHECK(!runtime.app.storage.write_blocked);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    decoded_sequence = 0U;
    CHECK(aurora_storage_classify_page(page, sizeof(page), &decoded, &decoded_sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(decoded_sequence == 2U);

    mock_fail_next_flash_program();
    mock_advance_ms(AURORA_STORAGE_DIRTY_HOLD_MS + 1U);
    aurora_runtime_poll(&runtime);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.write_failure_count == 2U);
    CHECK(runtime.app.storage.write_blocked);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    decoded_sequence = 0U;
    CHECK(aurora_storage_classify_page(page, sizeof(page), &decoded, &decoded_sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(decoded_sequence == 2U);

    erase_before_blocked_poll = mock_flash_erase_count();
    program_before_blocked_poll = mock_flash_program_count();
    mock_advance_ms(AURORA_STORAGE_DIRTY_HOLD_MS + 1U);
    aurora_runtime_poll(&runtime);
    CHECK(mock_flash_erase_count() == erase_before_blocked_poll);
    CHECK(mock_flash_program_count() == program_before_blocked_poll);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_storage_tracks_physical_active_page_not_sequence_parity(void)
 * Input       : 无
 * Output      : 无
 * Description : 人工构造A页保存偶数sequence，验证修复逻辑仍写B页，绝不靠奇偶推断而擦掉唯一有效页。
 *---------------------------------------------------------------------------*/
static void test_storage_tracks_physical_active_page_not_sequence_parity(void)
{
    aurora_storage_ctx_t seed;
    aurora_runtime_t runtime;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];
    size_t used;

    mock_reset();
    aurora_storage_init_defaults(&seed);
    seed.sequence = 2U;
    used = aurora_storage_encode_page(&seed, page, sizeof(page), true);
    CHECK(used == AURORA_STORAGE_ENCODED_SIZE);
    CHECK(drv_flash_erase_page(drv_board_flash_page_a()));
    CHECK(drv_flash_program(drv_board_flash_page_a(), page, used));

    CHECK(aurora_runtime_init(&runtime));
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.dirty);
    mock_advance_ms(AURORA_STORAGE_DIRTY_HOLD_MS + 1U);
    aurora_runtime_poll(&runtime);
    CHECK(runtime.app.storage.sequence == 3U);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_B);
}

'''
anchor = "/*---------------------------------------------------------------------------*\n * Name        : int main(void)"
if anchor not in p:
    raise SystemExit("test main anchor missing")
p = p.replace(anchor, insert_tests + anchor, 1)
p = replace_once(
    p,
    "    test_legacy_energy_fields_keep_v0102_pv_semantics();\n",
    "    test_legacy_energy_fields_keep_v0102_pv_semantics();\n"
    "    test_storage_driver_rejects_out_of_bounds_and_cross_page();\n"
    "    test_storage_writes_only_in_stopped_states();\n"
    "    test_storage_failed_write_preserves_last_good_and_bounds_retry();\n"
    "    test_storage_tracks_physical_active_page_not_sequence_parity();\n",
    "test main storage calls",
)
write("tests/test_v0103.c", p)

write(
    "tests/test_flash_storage_contract.py",
    r'''from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FlashStorageSafetyContract(unittest.TestCase):
    def test_flash_driver_is_hard_bounded_to_final_two_pages(self):
        board = (ROOT / "driver/inc/board_config.h").read_text(encoding="utf-8")
        flash = (ROOT / "driver/src/drv_flash.c").read_text(encoding="utf-8")
        scatter = (ROOT / "project/AuroraControl.sct").read_text(encoding="utf-8")
        self.assertIn("BOARD_FLASH_TOTAL_SIZE_BYTES", board)
        self.assertIn("0x0000FC00UL", board)
        self.assertIn("0x0000FE00UL", board)
        self.assertIn("range_in_single_nvm_page", flash)
        self.assertIn("Flash Journal must occupy the final two physical pages only", flash)
        self.assertIn("LR_IROM1 0x00000000 0x0000FC00", scatter)

    def test_storage_layout_is_checked_at_compile_time(self):
        storage = (ROOT / "app/inc/storage.h").read_text(encoding="utf-8")
        self.assertIn("AURORA_STORAGE_ENCODED_SIZE", storage)
        self.assertIn("AURORA_STORAGE_PAYLOAD_USED_SIZE", storage)
        self.assertIn("Flash v3 payload layout does not match", storage)
        self.assertIn("Flash v3 encoded record exceeds one physical page", storage)

    def test_runtime_writes_only_in_stopped_states_and_bounds_retry(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        self.assertIn("runtime_storage_stop_state", main)
        for state in ("AURORA_POWER_WAIT_PV", "AURORA_POWER_NO_SUN", "AURORA_POWER_FAULT", "AURORA_POWER_OFF"):
            self.assertIn(state, main)
        self.assertIn("runtime_storage_program_page", main)
        self.assertIn("write_blocked", main)
        self.assertIn("AURORA_STORAGE_WRITE_RETRY_MAX", config)
        self.assertNotIn("drv_system_supply_is_good", main[main.index("static void runtime_storage"):main.index("static void runtime_watchdog")])

    def test_pvd_remains_boot_only_not_a_flash_save_trigger(self):
        system = (ROOT / "driver/src/drv_system.c").read_text(encoding="utf-8")
        self.assertIn("PVD is a boot qualifier, not a reset source", system)
        self.assertNotIn("drv_flash_program", system)
        self.assertNotIn("aurora_storage_mark_dirty", system)


if __name__ == "__main__":
    unittest.main()
''',
)

# -----------------------------------------------------------------------------
# 6. Documentation: capture 120W failure lesson and 300W storage contract.
# -----------------------------------------------------------------------------
write(
    "docs/48-v0.10.3-Flash边界与停机保存闭环.md",
    r'''# 48 · v0.10.3 Flash 边界与停机保存闭环

## 1. 背景

120W 旧工程的虚拟 EEPROM 曾出现过一种高危失效链：EEPROM 变量表长度与实际地址数量不一致、初始化失败返回值未形成写禁止条件、活动写地址保持 BSS 初值 0，随后写接口可能把地址 `0x00000000` 当成合法目标，最终破坏向量表/Boot 区。演示模式无电池缓冲时电源跌落更陡，更容易放大“掉电/重启 + Flash 状态恢复失败”的窗口。

300W 工程不移植旧式 `guActiveAddress`/`TransferPage` EEPROM 仿真，而使用最后两页固定地址的双页 Flash Journal。本文把旧问题转化为 300W 必须永久满足的安全合同。

## 2. 300W 固定 Flash 布局

```text
0x00000000 ... 0x0000FBFF   固件代码/常量（Scatter限制到0xFC00之前）
0x0000FC00 ... 0x0000FDFF   Journal Page A，512B
0x0000FE00 ... 0x0000FFFF   Journal Page B，512B
```

`project/AuroraControl.sct`、`board_config.h` 与 `drv_flash.c` 三处必须一致。Driver只接受最后1KiB范围；编程请求还必须完整落在单个A页或B页内，禁止0长度、地址0、越界和跨页写。

## 3. 不使用欠压/LVD/PVD抢写 Flash

300W 运行期不在欠压事件中执行 Flash 擦写。PVD只用于上电供电资格：

```text
复位
→ PVD确认VDD稳定
→ 关闭PVD资格监视
→ 正常运行
```

突然掉电时允许丢失尚未落盘的RAM增量，但禁止为了“抢最后一笔”在电压已经塌陷时启动擦除/编程。安全目标优先级是：

1. 不破坏程序区；
2. 不同时破坏两份Journal；
3. 最后才是尽量保存最新统计量。

## 4. 只在明确停机状态写入

即使PWM和Relay当前恰好为OFF，也不足以证明可以写Flash。运行层只允许以下功率状态触发保存：

```text
WAIT_PV
NO_SUN
FAULT
OFF
```

并继续同时要求：

```text
PWM物理关闭
Relay物理断开
dirty保持时间满足
write_blocked == false
```

PRECHARGE、RELAY_HOLD_OFF、RELAY_SETTLE、BAT_STABILITY、RUN、Demo Probe/Run等状态全部禁止写入。

## 5. 双页事务规则

每次写入遵循：

```text
保留 current_sequence / active_page（上一份有效证据）
→ 选择 active_page 的另一物理页
→ 用 next_sequence 在RAM编码未提交页
→ 恢复RAM current_sequence
→ 擦目标页
→ 写除Commit Marker外的数据
→ 最后写Commit Marker
→ 逐4字节回读验证
→ 全部成功后才更新RAM sequence和active_page
```

任何一步失败：

```text
sequence不前移
active_page不切换
上一份有效页不擦除
保持dirty
1s后最多重试一次
再次失败后write_blocked=true，本次上电不再擦写
锁存STORAGE故障
```

因此不会出现“第一次失败后sequence先加1，第二次重试改擦上一份好页”的放大链。

## 6. 为什么不再依赖 sequence 奇偶选择页

正常写入时奇偶确实可以形成A/B交替，但异常恢复、历史页、人工烧录或未来格式迁移不能假设“奇数一定在A、偶数一定在B”。运行上下文明确保存最后有效记录来自 `PAGE_A/PAGE_B`，下一笔永远写另一页。sequence只负责新旧排序，不负责决定物理地址。

## 7. 编译期布局保护

`storage.h` 编译期验证：

- v3最后一个字段结束位置必须等于 `AURORA_STORAGE_PAYLOAD_SIZE`；
- Header + Payload 必须小于等于512B；
- 总编码长度保持4字节对齐；
- Commit Marker必须位于固定Header内。

这对应旧120W `EEPROM1_EMU_LENGTH` 与实际变量数量不一致的教训：**变量布局变化必须让编译立即失败，而不是运行数月后靠页迁移才暴露。**

## 8. 验收

Host回归必须覆盖：

1. 地址0写入被拒绝；
2. A页尾跨B页写入被拒绝；
3. PRECHARGE即使PWM/Relay均OFF也不写Flash；
4. WAIT_PV停机态允许正常事务；
5. 编程失败后sequence/active_page保持上一份有效值；
6. 连续两次失败后停止本次上电继续擦写；
7. 构造“A页偶数sequence”仍写B页，证明不依赖奇偶推断。

实板阶段还需要做断电注入：在擦除前、数据写中、Commit前、Commit后分别切断辅助供电，确认每次重启至少能找到一份有效页，且程序向量区保持不变。
''',
)

p = read("docs/00-文档索引.md")
p = replace_once(
    p,
    "| 47 | [v0.10.3-审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md) | 二次审核发现的Relay/ADC/能量、序号回绕、Host门禁与Bring-up边界闭环 |\n",
    "| 47 | [v0.10.3-审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md) | 二次审核发现的Relay/ADC/能量、序号回绕、Host门禁与Bring-up边界闭环 |\n"
    "| 48 | [v0.10.3-Flash边界与停机保存闭环](48-v0.10.3-Flash边界与停机保存闭环.md) | 吸取120W虚拟EEPROM越界/掉电教训：固定双页边界、停机写、Commit-last、回读验证和有限重试 |\n",
    "docs index 48",
)
write("docs/00-文档索引.md", p)

# Run the real permanent quality gate against the patched tree before committing.
subprocess.run(["python", "tools/run_checks.py"], cwd=ROOT, check=True)

# The temporary patch carrier must never enter the final tree/history commit.
for rel in ("tools/_flash_stop_save_closeout.py", ".github/workflows/_flash_stop_save_closeout.yml"):
    target = ROOT / rel
    if target.exists():
        target.unlink()

# Run once more on exactly the tree that will be committed.
subprocess.run(["python", "tools/run_checks.py"], cwd=ROOT, check=True)

subprocess.run(["git", "config", "user.name", "github-actions[bot]"], cwd=ROOT, check=True)
subprocess.run(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"], cwd=ROOT, check=True)
subprocess.run(["git", "add", "-A"], cwd=ROOT, check=True)
subprocess.run([
    "git", "commit", "-m",
    "修复：闭环Flash边界与停机保存，规避旧EEPROM掉电变砖风险"
], cwd=ROOT, check=True)
subprocess.run(["git", "push", "origin", "HEAD:codex/flash-stop-save-guard"], cwd=ROOT, check=True)
