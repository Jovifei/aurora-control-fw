#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, new_region: str) -> None:
    text = read(path)
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    write(path, text[:start] + new_region + text[end:])


# ---------------------------------------------------------------------------
# 1. APP存储策略：运行中只置dirty，物理Flash只在稳定停机状态落盘。
# ---------------------------------------------------------------------------
replace_once(
    "app/inc/app_config.h",
    """#define AURORA_STORAGE_DIRTY_HOLD_MS                (1000U)\n/* 运行中每60s提出一次能量保存请求；真正擦写仍只在PWM OFF且Relay OFF安全窗口。 */""",
    """#define AURORA_STORAGE_DIRTY_HOLD_MS                (1000U)\n/* 进入OFF/WAIT_PV/NO_SUN/FAULT后还需稳定保持1s，禁止在启动/关波瞬态擦写Flash。 */\n#define AURORA_STORAGE_STOP_HOLD_MS                 (1000U)\n/* 单次事务I/O或回读失败时只允许在同一备用页重试一次，禁止切页追写。 */\n#define AURORA_STORAGE_WRITE_ATTEMPTS               (2U)\n/* 运行中每60s只提出RAM保存请求；真正擦写必须等待稳定停机且VDD资格有效。 */""",
)

replace_once(
    "app/inc/storage.h",
    """/* Flash双页Journal运行状态；区分工厂擦除、半写、版本、CRC和内容错误。 */""" if False else """/* Flash双页Journal运行状态。 */\ntypedef struct\n{""",
    """/* 当前可信Journal页；NONE表示尚无已提交的可信页。 */\ntypedef uint8_t aurora_storage_active_page_t;\n#define AURORA_STORAGE_ACTIVE_NONE                  ((aurora_storage_active_page_t)0U)\n#define AURORA_STORAGE_ACTIVE_PAGE_A                ((aurora_storage_active_page_t)1U)\n#define AURORA_STORAGE_ACTIVE_PAGE_B                ((aurora_storage_active_page_t)2U)\n\n/* Flash双页Journal运行状态。 */\ntypedef struct\n{""",
)

replace_once(
    "app/inc/storage.h",
    """    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */\n    bool repair_pending;                             /* true表示另一页需在安全窗口重建冗余。 */\n    aurora_storage_page_status_t page_a_status;      /* 最近启动读取A页分类。 */\n    aurora_storage_page_status_t page_b_status;      /* 最近启动读取B页分类。 */""",
    """    bool dirty;                                      /* true表示RAM设置尚未写入Flash。 */\n    bool repair_pending;                             /* true表示另一页需在安全窗口重建冗余。 */\n    bool write_inhibited;                            /* 连续安全供电I/O失败后禁止本会话继续擦写。 */\n    aurora_storage_active_page_t active_page;        /* 当前仍可信的已提交页，写失败时不得切换。 */\n    aurora_storage_page_status_t page_a_status;      /* 最近启动读取A页分类。 */\n    aurora_storage_page_status_t page_b_status;      /* 最近启动读取B页分类。 */""",
)

# ---------------------------------------------------------------------------
# 2. PVD仅作为Flash写入veto：绝不在欠压事件里触发保存。
# ---------------------------------------------------------------------------
replace_once(
    "driver/inc/drv_system.h",
    """bool drv_system_supply_is_good(void);\nbool drv_system_wait_for_supply_stable(void);""",
    """bool drv_system_supply_is_good(void);\nbool drv_system_flash_supply_is_safe(void);\nbool drv_system_wait_for_supply_stable(void);""",
)

replace_once(
    "driver/src/drv_system.c",
    """ * Description : 供电资格通过或PVD模块异常后关闭PVD；正常运行阶段不使用PVD做弱光保护。""",
    """ * Description : 仅在PVD模块建立异常时关闭监测；正常启动后保留只读PVD供Flash写入veto使用。""",
)

insert_marker = """/*---------------------------------------------------------------------------*\n * Name        : void drv_system_supply_qualifier_stop(void)"""
system_insert = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_system_flash_supply_is_safe(void)\n * Input       : 无\n * Output      : true表示PVD已就绪且当前VDD高于启动资格门限\n * Description : 仅作为Flash擦写前的否决条件；不触发保存、不产生欠压Fault，也不使能PVD IRQ/Reset。\n *---------------------------------------------------------------------------*/\nbool drv_system_flash_supply_is_safe(void)\n{\n    return (DDL_PMU_IsEnabledPVD() != 0U) && drv_system_supply_monitor_ready() &&\n           drv_system_supply_is_good();\n}\n\n"""
replace_once("driver/src/drv_system.c", insert_marker, system_insert + insert_marker)

wait_start = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_system_wait_for_supply_stable(void)"""
wait_end = """/*---------------------------------------------------------------------------*\n * Name        : aurora_irq_state_t drv_irq_save(void)"""
wait_region = read("driver/src/drv_system.c")
start = wait_region.index(wait_start)
end = wait_region.index(wait_end, start)
old_wait = wait_region[start:end]
needle = """    drv_system_supply_qualifier_stop();\n    return true;\n}\n\n"""
if needle not in old_wait:
    raise SystemExit("drv_system.c: successful supply qualifier tail not found")
new_wait = old_wait.replace(
    needle,
    """    /* 正常启动后保留PVD为无中断、无复位的只读电源监测器；Flash只读取它做写入veto。 */\n    return true;\n}\n\n""",
    1,
)
write("driver/src/drv_system.c", wait_region[:start] + new_wait + wait_region[end:])

# ---------------------------------------------------------------------------
# 3. Driver层最后防线：地址0、跨页写、低VDD、PWM运行全部拒绝。
# ---------------------------------------------------------------------------
range_start = """/*---------------------------------------------------------------------------*\n * Name        : static bool range_in_nvm(uint32_t address, size_t length)"""
range_end = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_read(uint32_t address, void *data, size_t length)"""
new_range = """/*---------------------------------------------------------------------------*\n * Name        : static bool range_in_nvm(uint32_t address, size_t length)\n * Input       : address - Flash地址；length - 数据长度\n * Output      : true表示请求范围完全位于双页NVM保留区\n * Description : 使用64位端地址检查，地址0和整数回绕都不能绕过0xFC00~0xFFFF边界。\n *---------------------------------------------------------------------------*/\nstatic bool range_in_nvm(uint32_t address, size_t length)\n{\n    const uint32_t first = BOARD_FLASH_PAGE_A_ADDRESS;\n    const uint32_t last = BOARD_FLASH_PAGE_B_ADDRESS + BOARD_FLASH_PAGE_SIZE;\n    const uint64_t end = (uint64_t)address + (uint64_t)length;\n\n    return (length != 0U) && (address >= first) && (end <= (uint64_t)last);\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : static bool range_in_single_nvm_page(uint32_t address, size_t length)\n * Input       : address - Flash地址；length - 数据长度\n * Output      : true表示请求完整落在A页或B页之一\n * Description : 编程操作禁止一次跨越两个Journal页，避免单个长度/偏移Bug同时破坏A/B冗余。\n *---------------------------------------------------------------------------*/\nstatic bool range_in_single_nvm_page(uint32_t address, size_t length)\n{\n    const uint64_t end = (uint64_t)address + (uint64_t)length;\n    const uint64_t a_first = BOARD_FLASH_PAGE_A_ADDRESS;\n    const uint64_t a_last = a_first + BOARD_FLASH_PAGE_SIZE;\n    const uint64_t b_first = BOARD_FLASH_PAGE_B_ADDRESS;\n    const uint64_t b_last = b_first + BOARD_FLASH_PAGE_SIZE;\n\n    if (length == 0U)\n    {\n        return false;\n    }\n    return (((uint64_t)address >= a_first) && (end <= a_last)) ||\n           (((uint64_t)address >= b_first) && (end <= b_last));\n}\n\n"""
replace_region("driver/src/drv_flash.c", range_start, range_end, new_range)

replace_once(
    "driver/src/drv_flash.c",
    """    if (((address != BOARD_FLASH_PAGE_A_ADDRESS) && (address != BOARD_FLASH_PAGE_B_ADDRESS)) ||\n        drv_pwm_output_active())""",
    """    if (((address != BOARD_FLASH_PAGE_A_ADDRESS) && (address != BOARD_FLASH_PAGE_B_ADDRESS)) ||\n        drv_pwm_output_active() || !drv_system_flash_supply_is_safe())""",
)
replace_once(
    "driver/src/drv_flash.c",
    """    if ((data == NULL) || ((address & 3U) != 0U) || ((length & 3U) != 0U) ||\n        !range_in_nvm(address, length) || drv_pwm_output_active())""",
    """    if ((data == NULL) || ((address & 3U) != 0U) || ((length & 3U) != 0U) ||\n        !range_in_single_nvm_page(address, length) || drv_pwm_output_active() ||\n        !drv_system_flash_supply_is_safe())""",
)
replace_once(
    "driver/src/drv_flash.c",
    """ * Description : 仅在地址为A/B页且PWM未输出时擦除一个参数页，并在操作前后管理Flash解锁。""",
    """ * Description : 仅允许A/B页、PWM关闭且VDD资格有效时擦除；欠压只拒绝写，不触发掉电保存。""",
)
replace_once(
    "driver/src/drv_flash.c",
    """ * Description : 仅在地址/长度4字节对齐、范围合法且PWM关闭时编程内部Flash。""",
    """ * Description : 仅在单页范围、4字节对齐、PWM关闭且VDD资格有效时编程，禁止地址0和跨A/B页写。""",
)

# ---------------------------------------------------------------------------
# 4. Runtime Journal：先写备用页，成功+回读后才提交sequence/active_page。
# ---------------------------------------------------------------------------
replace_once(
    "app/src/main.c",
    """        if (choose_b)\n        {\n            runtime->app.storage.settings = settings_b;\n            runtime->app.storage.sequence = seq_b;\n        }\n        else\n        {\n            runtime->app.storage.settings = settings_a;\n            runtime->app.storage.sequence = seq_a;\n        }""",
    """        if (choose_b)\n        {\n            runtime->app.storage.settings = settings_b;\n            runtime->app.storage.sequence = seq_b;\n            runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_PAGE_B;\n        }\n        else\n        {\n            runtime->app.storage.settings = settings_a;\n            runtime->app.storage.sequence = seq_a;\n            runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_PAGE_A;\n        }""",
)
replace_once(
    "app/src/main.c",
    """    if ((status_a == AURORA_STORAGE_PAGE_ERASED) && (status_b == AURORA_STORAGE_PAGE_ERASED))\n    {\n        aurora_storage_mark_dirty(&runtime->app.storage, now_ms);\n        return;\n    }\n\n    aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);""",
    """    if ((status_a == AURORA_STORAGE_PAGE_ERASED) && (status_b == AURORA_STORAGE_PAGE_ERASED))\n    {\n        runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_NONE;\n        aurora_storage_mark_dirty(&runtime->app.storage, now_ms);\n        return;\n    }\n\n    /* 两页均无可信记录时禁止本会话继续自动擦写，避免在未知配置上反复自愈。 */\n    runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_NONE;\n    runtime->app.storage.write_inhibited = true;\n    aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);""",
)

storage_start = """/*---------------------------------------------------------------------------*\n * Name        : static void runtime_storage(aurora_runtime_t *runtime,"""
storage_end = """/*---------------------------------------------------------------------------*\n * Name        : static void runtime_watchdog(aurora_runtime_t *runtime,"""
new_storage = """/*---------------------------------------------------------------------------*\n * Name        : static bool storage_state_allows_write(const aurora_runtime_t *runtime)\n * Input       : runtime - 应用运行上下文\n * Output      : true表示当前处于稳定停机类状态\n * Description : 只允许OFF/WAIT_PV/NO_SUN/FAULT落盘；启动、预充、Relay握手和RUN阶段一律只保留RAM dirty。\n *---------------------------------------------------------------------------*/\nstatic bool storage_state_allows_write(const aurora_runtime_t *runtime)\n{\n    switch (runtime->app.power_stage.state)\n    {\n    case AURORA_POWER_OFF:\n    case AURORA_POWER_WAIT_PV:\n    case AURORA_POWER_NO_SUN:\n    case AURORA_POWER_FAULT:\n        return true;\n\n    default:\n        return false;\n    }\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : static uint32_t storage_inactive_page(const aurora_storage_ctx_t *storage)\n * Input       : storage - Journal运行状态\n * Output      : 下一次只允许擦写的备用页地址；状态非法时返回0\n * Description : 永远写当前可信页的另一页；无可信页时从A页开始，失败重试仍保持同一目标页。\n *---------------------------------------------------------------------------*/\nstatic uint32_t storage_inactive_page(const aurora_storage_ctx_t *storage)\n{\n    if (storage->active_page == AURORA_STORAGE_ACTIVE_PAGE_A)\n    {\n        return drv_board_flash_page_b();\n    }\n    if (storage->active_page == AURORA_STORAGE_ACTIVE_PAGE_B)\n    {\n        return drv_board_flash_page_a();\n    }\n    if (storage->active_page == AURORA_STORAGE_ACTIVE_NONE)\n    {\n        return drv_board_flash_page_a();\n    }\n    return 0U;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : static aurora_status_t storage_write_transaction(\n *               const aurora_storage_ctx_t *staged, uint32_t target, uint8_t *page)\n * Input       : staged - 带下一序号的待提交镜像；target - 固定备用页；page - 512字节工作缓冲\n * Output      : OK成功；NOT_READY表示VDD失去资格需延后；INVALID/IO_ERROR表示真实事务错误\n * Description : 擦备用页→写正文→最后Commit→整页回读CRC/内容复核；任何阶段欠压只中止，不触发欠压保存。\n *---------------------------------------------------------------------------*/\nstatic aurora_status_t storage_write_transaction(const aurora_storage_ctx_t *staged,\n                                                  uint32_t target, uint8_t *page)\n{\n    aurora_persistent_settings_t verified_settings = {0};\n    uint32_t verified_sequence = 0U;\n    const uint32_t marker = AURORA_STORAGE_COMMIT_MARKER;\n    size_t used;\n\n    used = aurora_storage_encode_page(staged, page, AURORA_STORAGE_PAGE_SIZE, false);\n    if (used < AURORA_STORAGE_HEADER_SIZE)\n    {\n        return AURORA_STATUS_INVALID;\n    }\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_flash_erase_page(target))\n    {\n        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR\n                                                 : AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_flash_program(target, page, AURORA_STORAGE_COMMIT_OFFSET))\n    {\n        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR\n                                                 : AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t),\n                           &page[AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t)],\n                           used - AURORA_STORAGE_COMMIT_OFFSET - sizeof(uint32_t)))\n    {\n        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR\n                                                 : AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET, &marker, sizeof(marker)))\n    {\n        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR\n                                                 : AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return AURORA_STATUS_NOT_READY;\n    }\n    if (!drv_flash_read(target, page, AURORA_STORAGE_PAGE_SIZE) ||\n        (aurora_storage_classify_page(page, AURORA_STORAGE_PAGE_SIZE, &verified_settings,\n                                      &verified_sequence) != AURORA_STORAGE_PAGE_VALID) ||\n        (verified_sequence != staged->sequence))\n    {\n        return AURORA_STATUS_IO_ERROR;\n    }\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return AURORA_STATUS_NOT_READY;\n    }\n    return AURORA_STATUS_OK;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : static void runtime_storage(aurora_runtime_t *runtime,\n *               uint32_t now_ms)\n * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒\n * Output      : 无\n * Description : 只在稳定停机+Relay/PWM物理关闭+VDD资格有效时写备用页；成功回读后才切换Journal提交状态。\n *---------------------------------------------------------------------------*/\nstatic void runtime_storage(aurora_runtime_t *runtime, uint32_t now_ms)\n{\n    uint8_t page[AURORA_STORAGE_PAGE_SIZE];\n    aurora_storage_ctx_t staged;\n    aurora_status_t status = AURORA_STATUS_IO_ERROR;\n    uint32_t target;\n    uint8_t attempt;\n\n    if (!runtime->app.storage.dirty || runtime->app.storage.write_inhibited ||\n        ((now_ms - runtime->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||\n        !storage_state_allows_write(runtime) ||\n        ((now_ms - runtime->app.power_stage.state_since_ms) < AURORA_STORAGE_STOP_HOLD_MS) ||\n        drv_pwm_output_active() || runtime->relay_applied)\n    {\n        return;\n    }\n\n    /* 欠压/棕断只延后保存；绝不把LVD/PVD事件当成一次“最后写Flash”的触发源。 */\n    if (!drv_system_flash_supply_is_safe())\n    {\n        return;\n    }\n\n    target = storage_inactive_page(&runtime->app.storage);\n    if (target == 0U)\n    {\n        runtime->app.storage.write_inhibited = true;\n        aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);\n        return;\n    }\n\n    staged = runtime->app.storage;\n    staged.sequence = runtime->app.storage.sequence + 1U;\n\n    for (attempt = 0U; attempt < AURORA_STORAGE_WRITE_ATTEMPTS; ++attempt)\n    {\n        status = storage_write_transaction(&staged, target, page);\n        if (status == AURORA_STATUS_OK)\n        {\n            runtime->app.storage.sequence = staged.sequence;\n            runtime->app.storage.active_page =\n                (target == drv_board_flash_page_a()) ? AURORA_STORAGE_ACTIVE_PAGE_A\n                                                     : AURORA_STORAGE_ACTIVE_PAGE_B;\n            runtime->app.storage.dirty = false;\n            runtime->app.storage.repair_pending = false;\n            if (target == drv_board_flash_page_a())\n            {\n                runtime->app.storage.page_a_status = AURORA_STORAGE_PAGE_VALID;\n            }\n            else\n            {\n                runtime->app.storage.page_b_status = AURORA_STORAGE_PAGE_VALID;\n            }\n            return;\n        }\n        if (status == AURORA_STATUS_NOT_READY)\n        {\n            /* VDD资格丢失时保留旧active page和dirty，等待下一次稳定停机窗口。 */\n            return;\n        }\n        if (status == AURORA_STATUS_INVALID)\n        {\n            break;\n        }\n        /* IO_ERROR只允许在同一个target备用页上再试一次，绝不递增已提交sequence或切到active页。 */\n    }\n\n    runtime->app.storage.write_inhibited = true;\n    aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);\n}\n\n"""
replace_region("app/src/main.c", storage_start, storage_end, new_storage)

# ---------------------------------------------------------------------------
# 5. Host Mock：模拟电源veto、I/O失败和写次数，锁住回归。
# ---------------------------------------------------------------------------
replace_once(
    "tests/mock_driver.h",
    """bool mock_relay(void);\nuint16_t *mock_adc_block(uint8_t index);""",
    """bool mock_relay(void);\nuint16_t *mock_adc_block(uint8_t index);\nvoid mock_set_flash_supply_safe(bool safe);\nvoid mock_fail_next_flash_program(uint32_t count);\nuint32_t mock_flash_erase_count(void);\nuint32_t mock_flash_program_count(void);""",
)

replace_once(
    "tests/mock_driver.c",
    """static uint8_t g_flash[MOCK_FLASH_SIZE_BYTES];""",
    """static uint8_t g_flash[MOCK_FLASH_SIZE_BYTES];\nstatic bool g_flash_supply_safe;\nstatic uint32_t g_flash_fail_program_remaining;\nstatic uint32_t g_flash_erase_counter;\nstatic uint32_t g_flash_program_counter;""",
)
replace_once(
    "tests/mock_driver.c",
    """    g_uart_tx_length = 0U;\n    memset(g_flash, 0xFF, sizeof(g_flash));""",
    """    g_uart_tx_length = 0U;\n    g_flash_supply_safe = true;\n    g_flash_fail_program_remaining = 0U;\n    g_flash_erase_counter = 0U;\n    g_flash_program_counter = 0U;\n    memset(g_flash, 0xFF, sizeof(g_flash));""",
)

adc_marker = """/*---------------------------------------------------------------------------*\n * Name        : void drv_system_reset(void)"""
mock_helpers = """/*---------------------------------------------------------------------------*\n * Name        : void mock_set_flash_supply_safe(bool safe)\n * Input       : safe - true表示VDD满足Flash写入资格；false表示模拟欠压/棕断\n * Output      : 无\n * Description : 只改变Flash写入veto，不主动触发保存或Fault。\n *---------------------------------------------------------------------------*/\nvoid mock_set_flash_supply_safe(bool safe)\n{\n    g_flash_supply_safe = safe;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : void mock_fail_next_flash_program(uint32_t count)\n * Input       : count - 后续需要失败的program调用次数\n * Output      : 无\n * Description : 注入供电仍正常时的真实Flash编程I/O失败，用于验证同页一次重试和写入抑制。\n *---------------------------------------------------------------------------*/\nvoid mock_fail_next_flash_program(uint32_t count)\n{\n    g_flash_fail_program_remaining = count;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : uint32_t mock_flash_erase_count(void)\n * Input       : 无\n * Output      : 本测试累计的有效Flash擦除尝试次数\n * Description : 用于断言欠压和非停机状态不会触碰Flash。\n *---------------------------------------------------------------------------*/\nuint32_t mock_flash_erase_count(void)\n{\n    return g_flash_erase_counter;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : uint32_t mock_flash_program_count(void)\n * Input       : 无\n * Output      : 本测试累计的有效Flash编程尝试次数\n * Description : 用于验证事务写次数和失败重试边界。\n *---------------------------------------------------------------------------*/\nuint32_t mock_flash_program_count(void)\n{\n    return g_flash_program_counter;\n}\n\n/*---------------------------------------------------------------------------*\n * Name        : bool drv_system_flash_supply_is_safe(void)\n * Input       : 无\n * Output      : Host模拟的Flash供电资格\n * Description : 与目标PVD只读veto接口保持一致。\n *---------------------------------------------------------------------------*/\nbool drv_system_flash_supply_is_safe(void)\n{\n    return g_flash_supply_safe;\n}\n\n"""
replace_once("tests/mock_driver.c", adc_marker, mock_helpers + adc_marker)

# Mock program必须单页、供电正常，且支持I/O失败注入。
flash_erase_start = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_erase_page(uint32_t address)"""
flash_program_start = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_program(uint32_t address, const void *data, size_t length)"""
text = read("tests/mock_driver.c")
start = text.index(flash_erase_start)
end = text.index(flash_program_start, start)
old_erase = text[start:end]
body_start = old_erase.index("bool drv_flash_erase_page(uint32_t address)")
new_erase = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_erase_page(uint32_t address)\n * Input       : address - 待擦除页首地址\n * Output      : true表示整页已恢复为0xFF；false表示PWM/VDD/地址门禁或注入错误\n * Description : Host模拟与目标一致：只允许A/B整页、PWM关闭、VDD资格有效时擦除。\n *---------------------------------------------------------------------------*/\nbool drv_flash_erase_page(uint32_t address)\n{\n    size_t offset;\n\n    if (g_pwm_active || !g_flash_supply_safe ||\n        !flash_range(address, MOCK_FLASH_PAGE_SIZE_BYTES, &offset) ||\n        ((address != MOCK_FLASH_BASE_ADDRESS) &&\n         (address != (MOCK_FLASH_BASE_ADDRESS + MOCK_FLASH_PAGE_SIZE_BYTES))))\n    {\n        return false;\n    }\n\n    g_flash_erase_counter++;\n    memset(&g_flash[offset], 0xFF, MOCK_FLASH_PAGE_SIZE_BYTES);\n    return true;\n}\n\n"""
write("tests/mock_driver.c", text[:start] + new_erase + text[end:])

# 替换program函数到下一个函数头。
program_end_marker = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_watchdog_init"""
text = read("tests/mock_driver.c")
start = text.index(flash_program_start)
end = text.index(program_end_marker, start)
new_program = """/*---------------------------------------------------------------------------*\n * Name        : bool drv_flash_program(uint32_t address, const void *data, size_t length)\n * Input       : address - 起始编程地址；data - 数据；length - 长度\n * Output      : true表示编程完成；false表示地址/供电/PWM门禁或注入I/O失败\n * Description : 仅允许单个512字节Journal页内4字节对齐写入；地址0、跨页和欠压一律拒绝。\n *---------------------------------------------------------------------------*/\nbool drv_flash_program(uint32_t address, const void *data, size_t length)\n{\n    size_t offset;\n    size_t i;\n    const uint64_t end_address = (uint64_t)address + (uint64_t)length;\n    const uint64_t page_a_end = (uint64_t)MOCK_FLASH_BASE_ADDRESS + MOCK_FLASH_PAGE_SIZE_BYTES;\n    const uint64_t page_b_start = page_a_end;\n    const uint64_t page_b_end = page_b_start + MOCK_FLASH_PAGE_SIZE_BYTES;\n    const bool in_one_page =\n        (((uint64_t)address >= MOCK_FLASH_BASE_ADDRESS) && (end_address <= page_a_end)) ||\n        (((uint64_t)address >= page_b_start) && (end_address <= page_b_end));\n\n    if ((data == NULL) || (length == 0U) || g_pwm_active || !g_flash_supply_safe ||\n        ((address & 3U) != 0U) || ((length & 3U) != 0U) || !in_one_page ||\n        !flash_range(address, length, &offset))\n    {\n        return false;\n    }\n\n    g_flash_program_counter++;\n    if (g_flash_fail_program_remaining != 0U)\n    {\n        g_flash_fail_program_remaining--;\n        return false;\n    }\n\n    for (i = 0U; i < length; ++i)\n    {\n        g_flash[offset + i] &= ((const uint8_t *)data)[i];\n    }\n    return true;\n}\n\n"""
write("tests/mock_driver.c", text[:start] + new_program + text[end:])

# ---------------------------------------------------------------------------
# 6. C回归：欠压零写、非停机零写、同备用页重试、双失败保留active页、地址0/跨页拒绝。
# ---------------------------------------------------------------------------
test_main_marker = """/*---------------------------------------------------------------------------*\n * Name        : int main(void)"""
new_tests = r'''/*---------------------------------------------------------------------------*
 * Name        : static uint32_t storage_test_ready_ms(void)
 * Input       : 无
 * Output      : 满足dirty与停机稳定窗口后的测试时间
 * Description : 统一构造可触发安全存储调度的时间点。
 *---------------------------------------------------------------------------*/
static uint32_t storage_test_ready_ms(void)
{
    return AURORA_STORAGE_DIRTY_HOLD_MS + AURORA_STORAGE_STOP_HOLD_MS + 10U;
}

/*---------------------------------------------------------------------------*
 * Name        : static void prepare_stopped_dirty_runtime(aurora_runtime_t *runtime)
 * Input       : runtime - 待准备的运行上下文
 * Output      : 无
 * Description : 从擦除Flash启动，保持WAIT_PV停机态并等待足够时间，使dirty只差电源资格即可落盘。
 *---------------------------------------------------------------------------*/
static void prepare_stopped_dirty_runtime(aurora_runtime_t *runtime)
{
    mock_reset();
    CHECK(aurora_runtime_init(runtime));
    runtime->app.power_stage.state = AURORA_POWER_WAIT_PV;
    runtime->app.power_stage.state_since_ms = 0U;
    runtime->app.storage.dirty = true;
    runtime->app.storage.dirty_since_ms = 0U;
    mock_advance_ms(storage_test_ready_ms());
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_low_supply_is_defer_only(void)
 * Input       : 无
 * Output      : 无
 * Description : 欠压/PVD不合格时不得擦写Flash，也不得把“没保存”升级成Storage Fault；dirty保留待下次停机。
 *---------------------------------------------------------------------------*/
static void test_flash_low_supply_is_defer_only(void)
{
    aurora_runtime_t runtime;

    prepare_stopped_dirty_runtime(&runtime);
    mock_set_flash_supply_safe(false);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 0U);
    CHECK(mock_flash_program_count() == 0U);
    CHECK(runtime.app.storage.dirty);
    CHECK(!runtime.app.storage.write_inhibited);
    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_STORAGE) == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_only_writes_in_stable_stop_state(void)
 * Input       : 无
 * Output      : 无
 * Description : 即使PWM/Relay物理关闭，START_DELAY等启动瞬态也不得把RAM dirty写入Flash。
 *---------------------------------------------------------------------------*/
static void test_flash_only_writes_in_stable_stop_state(void)
{
    aurora_runtime_t runtime;

    prepare_stopped_dirty_runtime(&runtime);
    runtime.app.power_stage.state = AURORA_POWER_START_DELAY;
    runtime.app.power_stage.state_since_ms = 0U;
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 0U);
    CHECK(mock_flash_program_count() == 0U);
    CHECK(runtime.app.storage.dirty);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_safe_stop_commits_and_reads_back(void)
 * Input       : 无
 * Output      : 无
 * Description : 稳定停机且VDD正常时从无active页写A页，Commit回读有效后才清dirty并提交sequence。
 *---------------------------------------------------------------------------*/
static void test_flash_safe_stop_commits_and_reads_back(void)
{
    aurora_runtime_t runtime;
    aurora_persistent_settings_t restored;
    uint32_t sequence = 0U;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];

    prepare_stopped_dirty_runtime(&runtime);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 1U);
    CHECK(mock_flash_program_count() == 3U);
    CHECK(!runtime.app.storage.dirty);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    CHECK(aurora_storage_classify_page(page, sizeof(page), &restored, &sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(sequence == runtime.app.storage.sequence);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_retries_same_inactive_page_once(void)
 * Input       : 无
 * Output      : 无
 * Description : 第一次program I/O失败时只在同一备用A页重试一次，成功后才提交sequence且不产生Storage Fault。
 *---------------------------------------------------------------------------*/
static void test_flash_retries_same_inactive_page_once(void)
{
    aurora_runtime_t runtime;

    prepare_stopped_dirty_runtime(&runtime);
    mock_fail_next_flash_program(1U);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == 2U);
    CHECK(mock_flash_program_count() == 4U);
    CHECK(!runtime.app.storage.dirty);
    CHECK(!runtime.app.storage.write_inhibited);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.sequence == 2U);
    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_STORAGE) == 0U);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_double_failure_preserves_active_page(void)
 * Input       : 无
 * Output      : 无
 * Description : 已有A页有效时，B页两次写失败不得递增已提交sequence或回头擦A页；本会话随后禁止继续擦写。
 *---------------------------------------------------------------------------*/
static void test_flash_double_failure_preserves_active_page(void)
{
    aurora_runtime_t runtime;
    aurora_persistent_settings_t restored;
    uint32_t first_sequence;
    uint32_t restored_sequence = 0U;
    uint32_t erase_before;
    uint32_t program_before;
    uint8_t page[AURORA_STORAGE_PAGE_SIZE];

    prepare_stopped_dirty_runtime(&runtime);
    aurora_runtime_poll(&runtime);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    first_sequence = runtime.app.storage.sequence;

    aurora_storage_mark_dirty(&runtime.app.storage, drv_time_now_ms());
    mock_advance_ms(storage_test_ready_ms());
    runtime.app.power_stage.state_since_ms = drv_time_now_ms() - AURORA_STORAGE_STOP_HOLD_MS;
    erase_before = mock_flash_erase_count();
    program_before = mock_flash_program_count();
    mock_fail_next_flash_program(2U);
    aurora_runtime_poll(&runtime);

    CHECK(mock_flash_erase_count() == erase_before + 2U);
    CHECK(mock_flash_program_count() == program_before + 2U);
    CHECK(runtime.app.storage.sequence == first_sequence);
    CHECK(runtime.app.storage.active_page == AURORA_STORAGE_ACTIVE_PAGE_A);
    CHECK(runtime.app.storage.dirty);
    CHECK(runtime.app.storage.write_inhibited);
    CHECK((aurora_protection_fault_mask(&runtime.app.protection) & AURORA_FAULT_STORAGE) != 0U);
    CHECK(drv_flash_read(drv_board_flash_page_a(), page, sizeof(page)));
    CHECK(aurora_storage_classify_page(page, sizeof(page), &restored, &restored_sequence) ==
          AURORA_STORAGE_PAGE_VALID);
    CHECK(restored_sequence == first_sequence);

    erase_before = mock_flash_erase_count();
    program_before = mock_flash_program_count();
    mock_advance_ms(storage_test_ready_ms());
    aurora_runtime_poll(&runtime);
    CHECK(mock_flash_erase_count() == erase_before);
    CHECK(mock_flash_program_count() == program_before);
}

/*---------------------------------------------------------------------------*
 * Name        : static void test_flash_driver_rejects_zero_and_cross_page(void)
 * Input       : 无
 * Output      : 无
 * Description : Driver最后防线必须拒绝0地址以及从A页尾跨入B页的单次program请求。
 *---------------------------------------------------------------------------*/
static void test_flash_driver_rejects_zero_and_cross_page(void)
{
    const uint32_t words[2] = {0x12345678UL, 0xABCDEF00UL};
    const uint32_t cross = drv_board_flash_page_a() + AURORA_STORAGE_PAGE_SIZE - sizeof(uint32_t);

    mock_reset();
    CHECK(!drv_flash_program(0U, words, sizeof(uint32_t)));
    CHECK(!drv_flash_program(cross, words, sizeof(words)));
    CHECK(mock_flash_program_count() == 0U);
}

'''
replace_once("tests/test_v0103.c", test_main_marker, new_tests + test_main_marker)

replace_once(
    "tests/test_v0103.c",
    """    test_legacy_energy_fields_keep_charge_semantics();\n    printf(\"Aurora v0.10.3 reviewed closeout tests: %u assertions passed.\\n\", g_assertions);""",
    """    test_legacy_energy_fields_keep_charge_semantics();\n    test_flash_low_supply_is_defer_only();\n    test_flash_only_writes_in_stable_stop_state();\n    test_flash_safe_stop_commits_and_reads_back();\n    test_flash_retries_same_inactive_page_once();\n    test_flash_double_failure_preserves_active_page();\n    test_flash_driver_rejects_zero_and_cross_page();\n    printf(\"Aurora v0.10.3 reviewed closeout tests: %u assertions passed.\\n\", g_assertions);""",
)

# Python静态契约锁死：不允许以后重新引入欠压写、pre-increment或跨页编程。
write(
    "tests/test_flash_safe_persistence_contract.py",
    '''from pathlib import Path\nimport unittest\n\nROOT = Path(__file__).resolve().parents[1]\n\n\nclass FlashSafePersistenceContract(unittest.TestCase):\n    def test_runtime_only_writes_in_stable_stop_with_supply_veto(self):\n        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")\n        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")\n        self.assertIn("storage_state_allows_write", main)\n        self.assertIn("AURORA_STORAGE_STOP_HOLD_MS", main)\n        self.assertIn("drv_system_flash_supply_is_safe()", main)\n        self.assertIn("AURORA_STORAGE_WRITE_ATTEMPTS", config)\n        storage = main[main.index("static void runtime_storage"):main.index("static void runtime_watchdog")]\n        self.assertNotIn("runtime->app.storage.sequence++;", storage)\n        self.assertIn("staged.sequence = runtime->app.storage.sequence + 1U;", storage)\n        self.assertIn("runtime->app.storage.write_inhibited = true;", storage)\n\n    def test_driver_rejects_address_zero_cross_page_and_low_supply(self):\n        flash = (ROOT / "driver/src/drv_flash.c").read_text(encoding="utf-8")\n        self.assertIn("range_in_single_nvm_page", flash)\n        self.assertIn("!drv_system_flash_supply_is_safe()", flash)\n        self.assertIn("address >= first", flash)\n        self.assertIn("end <= a_last", flash)\n        self.assertIn("end <= b_last", flash)\n\n    def test_pvd_is_veto_not_power_fail_write_trigger(self):\n        system = (ROOT / "driver/src/drv_system.c").read_text(encoding="utf-8")\n        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")\n        self.assertIn("bool drv_system_flash_supply_is_safe(void)", system)\n        self.assertIn("DDL_PMU_DisableIT_PVD();", system)\n        self.assertIn("DDL_RCC_Disable_PVDRST();", system)\n        self.assertNotIn("AURORA_FAULT_PVD", main)\n        self.assertNotIn("power_fail", main.lower())\n\n\nif __name__ == "__main__":\n    unittest.main()\n''',
)

# ---------------------------------------------------------------------------
# 7. 文档：明确老120W失效模式已规避、当前仍需防brownout写与双页误擦。
# ---------------------------------------------------------------------------
replace_once(
    "docs/09-内部Flash参数保存.md",
    """PWM物理关闭\nAND Runtime记录的Relay GPIO为OFF\nAND dirty保持至少1s""",
    """PowerStage处于 OFF / WAIT_PV / NO_SUN / FAULT 稳定停机类状态\nAND 该停机状态保持至少 AURORA_STORAGE_STOP_HOLD_MS\nAND PWM物理关闭\nAND Runtime记录的Relay GPIO为OFF\nAND dirty保持至少1s\nAND PVD只读监测确认VDD仍高于资格门限""",
)
replace_once(
    "docs/09-内部Flash参数保存.md",
    """当前软件没有运行期PVD/LVD掉电保存窗口，也没有FRAM/MRAM。正常Battery运行中Relay可能连续闭合数小时，dirty请求会一直等待安全窗口。""",
    """当前软件**故意不实现PVD/LVD掉电写Flash**，也没有FRAM/MRAM。PVD在正常启动后仅保留为无IRQ、无Reset的Flash写入veto：一旦VDD不满足资格，当前事务立即延后，绝不把欠压事件当成“最后保存”触发源。正常Battery运行中Relay可能连续闭合数小时，dirty请求会一直等待稳定停机窗口。""",
)
replace_once(
    "docs/09-内部Flash参数保存.md",
    """- 不在PWM或Relay活动时擦写；\n- NO_SUN、故障退出、模式切换后进入安全窗口时尽快落盘；\n- 不用周期性断Relay换取数据保存。""",
    """- 不在PWM或Relay活动时擦写；\n- 不在START_DELAY、ZERO_CAL、PRECHARGE、Relay握手等启动/切换瞬态擦写；\n- NO_SUN、WAIT_PV、OFF或故障停机稳定后且VDD资格仍有效时落盘；\n- 不在欠压/PVD事件中做所谓“最后一次保存”；\n- 单页写失败只在**同一备用页**重试一次，成功回读前不递增已提交sequence、不改变active页；\n- 两次真实I/O/回读失败后本会话禁止继续擦写，保留上一份有效页并锁存Storage Fault；\n- 不用周期性断Relay换取数据保存。""",
)

write(
    "docs/48-v0.10.3-Flash安全持久化与最近三次远端提交审核.md",
    '''# 48 · v0.10.3 Flash安全持久化与最近三次远端提交审核\n\n## 1. 最近三次远端提交审核\n\n审核基线：`main` 的 `53ddbe5f02fd6283c6572d1b79742e64c8479b5a`，向前检查最近三次提交。\n\n| Commit | 实际内容 | 审核结论 |\n|---|---|---|\n| `53ddbe5` | G2~G5文档/RESULT更新；`driver/src/drv_adc.c`仅7行注释替换 | 未发现由该提交新增的控制语义Bug |\n| `1f5d04c` | docs/bringup、tasks与46号路线文档 | 无运行时代码变更；注意README曾声明不含`tasks/`，需以当前Bring-up工作流为准 |\n| `08987c7` | 实际只新增`.codegraph/.gitignore` | Commit message与实际内容不一致，但不影响固件运行 |\n\n本轮没有把“最近三次提交”与“当前主线既有风险”混为一谈。真正需要修复的是当前Flash Journal在brownout/失败重试下的持久化边界。\n\n## 2. 与120W模拟EEPROM旧失效模式的对照\n\n120W旧实现的高风险链条是：Init失败或页满后活动地址保持BSS零值，随后未校验地把数据写到`0x00000000`，可能破坏IAP/向量表。\n\n300W当前架构已经具备两层基础隔离：\n\n1. Scatter把应用区限制到`0x00000000~0x0000FBFF`，最后1KiB独立留给A/B Journal；\n2. `drv_flash`只接受`0xFC00~0xFFFF`保留区，地址0直接拒绝。\n\n因此不迁移120W的活动地址式EEPROM仿真，而是在现有双页Journal上继续加固。\n\n## 3. 本轮发现并关闭的两个关键风险\n\n### 3.1 写失败前提前递增sequence\n\n旧`runtime_storage()`在真正擦写前先`sequence++`，再按奇偶选择页。第一次写失败后RAM sequence已经变化，下一轮可能切换到另一页，存在把上一份有效页也擦掉的放大风险。\n\n修复后：\n\n- 使用`staged.sequence = committed.sequence + 1`；\n- 永远只写`active_page`的另一页；\n- I/O失败只在同一个备用页重试一次；\n- Commit + 整页回读 + CRC/内容/sequence验证全部成功后，才更新RAM中的`sequence`和`active_page`；\n- 两次真实失败后`write_inhibited=true`，本会话不再反复擦页。\n\n### 3.2 欠压/掉电边沿仍可能进入Flash擦写\n\n旧门禁只要求PWM OFF、Relay OFF、dirty满1s。Demo/弱光场景下，即使系统正在接近brownout，只要这三个条件碰巧满足，仍可能开始擦页。\n\n修复后：\n\n- 物理写只允许`OFF / WAIT_PV / NO_SUN / FAULT`稳定停机态；\n- 启动、预充、Relay握手、RUN阶段只在RAM里置dirty；\n- PVD正常启动后保留为只读监测，IRQ/Reset继续关闭；\n- PVD/VDD不合格只**拒绝**本次Flash写，绝不触发“欠压保存”；\n- Driver层再次检查VDD资格，避免未来其他调用者绕过Runtime策略。\n\n## 4. 地址与双页边界\n\n`drv_flash_program()`现在要求一次请求完整落在单个512字节页内。即使未来出现offset/length计算错误，也不能用一次program跨越A/B两个冗余页，更不能写到地址0。\n\n## 5. 数据保持边界\n\n该策略选择“宁可丢最后一段RAM统计，也不能在brownout里赌一次Flash写”。若产品以后要求严格掉电保存，应通过可验证的保持电容 + 提前量，或FRAM/MRAM解决，而不是重新引入LVD/PVD中断写Flash。\n\n## 6. 发布边界\n\n本轮属于软件持久化安全修复。它不能证明Flash供电门限、掉电斜率和实板保持时间已经通过硬件验证；生产功率门仍保持原状态。\n''',
)

replace_once(
    "docs/00-文档索引.md",
    """| 47 | [v0.10.3-审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md) | 二次审核发现的Relay/ADC/能量、序号回绕、Host门禁与Bring-up边界闭环 |\n| 程序代码分析""",
    """| 47 | [v0.10.3-审阅问题补强与验证记录](47-v0.10.3-审阅问题补强与验证记录.md) | 二次审核发现的Relay/ADC/能量、序号回绕、Host门禁与Bring-up边界闭环 |\n| 48 | [v0.10.3-Flash安全持久化与最近三次远端提交审核](48-v0.10.3-Flash安全持久化与最近三次远端提交审核.md) | 最近三次远端提交复核、120W EEPROM失效模式对照、欠压写入veto与双页失败保护 |\n| 程序代码分析""",
)

# Bring-up已明确引入tasks目录，README不再声称仓库不包含tasks。
replace_once(
    "README.md",
    """仓库不包含旧MCU工程、闭源MPPT库、`legacy_*`、`tasks/`、重复的`firmware/tests/tools`、应用目录内厂商例程、生成JSON或bootstrap临时文件。""",
    """仓库不包含旧MCU工程、闭源MPPT库、`legacy_*`、重复的`firmware/tests/tools`、应用目录内厂商例程、生成JSON或bootstrap临时文件；`tasks/`现用于G0~G15迁移任务与经验记录。""",
)

print("Flash安全停机持久化修复已应用")
