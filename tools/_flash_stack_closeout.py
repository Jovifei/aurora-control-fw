#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


def replace_region(path: str, start_marker: str, end_marker: str, replacement: str) -> None:
    text = read(path)
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    write(path, text[:start] + replacement + text[end:])


# ---------------------------------------------------------------------------
# 1. Storage编码接口允许显式传入“待提交序号”，避免复制完整storage ctx到1KB目标栈。
# ---------------------------------------------------------------------------
replace_once(
    "app/inc/storage.h",
    """size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,\n                                  uint8_t *page,\n                                  size_t page_size,\n                                  bool committed);""",
    """size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,\n                                  uint8_t *page,\n                                  size_t page_size,\n                                  bool committed);\nsize_t aurora_storage_encode_page_sequence(const aurora_persistent_settings_t *settings,\n                                           uint32_t sequence,\n                                           uint8_t *page,\n                                           size_t page_size,\n                                           bool committed);""",
)

encode_start = """/*---------------------------------------------------------------------------*\n * Name        : size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,"""
encode_end = """/*---------------------------------------------------------------------------*\n * Name        : aurora_storage_page_status_t aurora_storage_classify_page("""
new_encode = r'''/*---------------------------------------------------------------------------*
 * Name        : size_t aurora_storage_encode_page_sequence(
 *               const aurora_persistent_settings_t *settings, uint32_t sequence,
 *               uint8_t *page, size_t page_size, bool committed)
 * Input       : settings - 待保存设置；sequence - 待提交序号；page/page_size - 目标页；
 *               committed - 是否写Commit Marker
 * Output      : 编码字节数；参数或内容无效时返回0
 * Description : 直接按settings+sequence编码v3页面，避免为了下一序号在1KB目标栈复制完整storage ctx。
 *---------------------------------------------------------------------------*/
size_t aurora_storage_encode_page_sequence(const aurora_persistent_settings_t *settings,
                                           uint32_t sequence, uint8_t *page, size_t page_size,
                                           bool committed)
{
    uint8_t *payload;
    size_t index;

    if ((settings == NULL) || (page == NULL) || (page_size < AURORA_STORAGE_PAGE_SIZE) ||
        !settings_valid(settings))
    {
        return 0U;
    }
    memset(page, 0xFF, AURORA_STORAGE_PAGE_SIZE);
    put_u32_le(&page[AURORA_STORAGE_MAGIC_OFFSET], AURORA_STORAGE_MAGIC);
    put_u16_le(&page[AURORA_STORAGE_VERSION_OFFSET], AURORA_STORAGE_VERSION);
    put_u16_le(&page[AURORA_STORAGE_LENGTH_OFFSET], AURORA_STORAGE_PAYLOAD_SIZE);
    put_u32_le(&page[AURORA_STORAGE_SEQUENCE_OFFSET], sequence);

    payload = &page[AURORA_STORAGE_HEADER_SIZE];
    payload[AURORA_STORAGE_CHEMISTRY_OFFSET] = (uint8_t)settings->chemistry;
    payload[AURORA_STORAGE_PACK_OFFSET] = (uint8_t)settings->pack;
    payload[AURORA_STORAGE_MODE_OFFSET] = (uint8_t)settings->operating_mode;
    payload[AURORA_STORAGE_HISTORY_COUNT_OFFSET] = settings->energy_history_count;
    payload[AURORA_STORAGE_ENERGY_SEMANTICS_OFFSET] = settings->energy_semantics_version;
    put_u32_le(&payload[AURORA_STORAGE_LIFETIME_ENERGY_OFFSET], settings->lifetime_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_DAILY_ENERGY_OFFSET], settings->daily_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_CHARGE_LIFETIME_OFFSET],
               settings->charge_est_lifetime_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_CHARGE_DAILY_OFFSET],
               settings->charge_est_daily_energy_wh);
    put_u32_le(&payload[AURORA_STORAGE_REVISION_OFFSET], settings->settings_revision);
    put_u32_le(&payload[AURORA_STORAGE_HISTORY_ELAPSED_OFFSET],
               settings->history_interval_elapsed_ms);
    put_u32_le(&payload[AURORA_STORAGE_DEMO_VOLTAGE_OFFSET], settings->demo_target_voltage_mv);
    put_u32_le(&payload[AURORA_STORAGE_DEMO_POWER_OFFSET], settings->demo_power_limit_mw);
    put_u64_le(&payload[AURORA_STORAGE_PV_REMAINDER_OFFSET], settings->pv_energy_remainder_mw_ms);
    put_u64_le(&payload[AURORA_STORAGE_CHARGE_REMAINDER_OFFSET],
               settings->charge_est_energy_remainder_mw_ms);

    for (index = 0U; index < AURORA_ENERGY_HISTORY_POINT_COUNT; ++index)
    {
        put_u32_le(&payload[AURORA_STORAGE_ENERGY_HISTORY_OFFSET + index * 4U],
                   settings->energy_history_wh[index]);
        put_u32_le(&payload[AURORA_STORAGE_CHARGE_HISTORY_OFFSET + index * 4U],
                   settings->charge_est_history_wh[index]);
    }

    put_u32_le(&page[AURORA_STORAGE_CRC_OFFSET],
               aurora_storage_crc32(payload, AURORA_STORAGE_PAYLOAD_SIZE));
    put_u32_le(&page[AURORA_STORAGE_COMMIT_OFFSET],
               committed ? AURORA_STORAGE_COMMIT_MARKER : STORAGE_ERASED_WORD);
    return AURORA_STORAGE_HEADER_SIZE + AURORA_STORAGE_PAYLOAD_SIZE;
}

/*---------------------------------------------------------------------------*
 * Name        : size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx,
 *               uint8_t *page, size_t page_size, bool committed)
 * Input       : ctx - 当前设置与已提交序号；page/page_size - 目标页；committed - 提交标志
 * Output      : 编码字节数；参数无效时返回0
 * Description : 保留原有公共接口，内部转调显式sequence编码器供既有测试/调用者兼容。
 *---------------------------------------------------------------------------*/
size_t aurora_storage_encode_page(const aurora_storage_ctx_t *ctx, uint8_t *page, size_t page_size,
                                  bool committed)
{
    if (ctx == NULL)
    {
        return 0U;
    }
    return aurora_storage_encode_page_sequence(&ctx->settings, ctx->sequence, page, page_size,
                                               committed);
}

'''
replace_region("app/src/storage.c", encode_start, encode_end, new_encode)

# ---------------------------------------------------------------------------
# 2. main.c只保留一个512B静态Journal工作页；load/save均不得在1KB目标栈放整页。
# ---------------------------------------------------------------------------
replace_once(
    "app/src/main.c",
    """/* 目标中断桥接与主循环共享的唯一运行实例。 */\naurora_runtime_t g_aurora_runtime;""",
    """/* 目标中断桥接与主循环共享的唯一运行实例。 */\naurora_runtime_t g_aurora_runtime;\n\n/* Flash Journal单线程工作页；避免512B页缓冲占用目标仅1KB的启动栈。 */\nstatic uint8_t g_storage_page_workspace[AURORA_STORAGE_PAGE_SIZE];""",
)

load_start = """/*---------------------------------------------------------------------------*\n * Name        : static void load_storage(aurora_runtime_t *runtime)"""
load_end = """/*---------------------------------------------------------------------------*\n * Name        : static bool storage_state_allows_write(const aurora_runtime_t *runtime)"""
new_load = r'''/*---------------------------------------------------------------------------*
 * Name        : static aurora_storage_page_status_t storage_read_page(
 *               uint32_t address, aurora_persistent_settings_t *settings,
 *               uint32_t *sequence)
 * Input       : address - A/B页地址；settings/sequence - 分类输出
 * Output      : 页分类状态
 * Description : 使用唯一静态512B工作页读取并分类，避免启动阶段在1KB目标栈分配整页。
 *---------------------------------------------------------------------------*/
static aurora_storage_page_status_t storage_read_page(uint32_t address,
                                                       aurora_persistent_settings_t *settings,
                                                       uint32_t *sequence)
{
    if (!drv_flash_read(address, g_storage_page_workspace, sizeof(g_storage_page_workspace)))
    {
        return AURORA_STORAGE_PAGE_IO_ERROR;
    }
    return aurora_storage_classify_page(g_storage_page_workspace, sizeof(g_storage_page_workspace),
                                        settings, sequence);
}

/*---------------------------------------------------------------------------*
 * Name        : static void load_storage(aurora_runtime_t *runtime)
 * Input       : runtime - 应用运行上下文
 * Output      : 无
 * Description : 先顺序检查A/B状态和序号，再只重读最终选中的可信页；不在栈同时保留两页和两套settings。
 *---------------------------------------------------------------------------*/
static void load_storage(aurora_runtime_t *runtime)
{
    aurora_storage_page_status_t status_a;
    aurora_storage_page_status_t status_b;
    aurora_storage_page_status_t selected_status;
    uint32_t seq_a = 0U;
    uint32_t seq_b = 0U;
    uint32_t selected_sequence = 0U;
    uint32_t selected_address;
    bool valid_a;
    bool valid_b;
    bool choose_b;
    const uint32_t now_ms = drv_time_now_ms();

    /* 首轮只收集A/B状态与序号；settings暂存可被下一页覆盖，最终选中后会重新读取。 */
    status_a = storage_read_page(drv_board_flash_page_a(), &runtime->app.storage.settings, &seq_a);
    status_b = storage_read_page(drv_board_flash_page_b(), &runtime->app.storage.settings, &seq_b);

    runtime->app.storage.page_a_status = status_a;
    runtime->app.storage.page_b_status = status_b;
    valid_a =
        (status_a == AURORA_STORAGE_PAGE_VALID) || (status_a == AURORA_STORAGE_PAGE_VALID_LEGACY);
    valid_b =
        (status_b == AURORA_STORAGE_PAGE_VALID) || (status_b == AURORA_STORAGE_PAGE_VALID_LEGACY);

    if (valid_a || valid_b)
    {
        choose_b = valid_b && (!valid_a || ((int32_t)(seq_b - seq_a) > 0));
        selected_address = choose_b ? drv_board_flash_page_b() : drv_board_flash_page_a();
        selected_status = storage_read_page(selected_address, &runtime->app.storage.settings,
                                            &selected_sequence);

        /* 第二次读取必须仍是同一可信记录；异常时禁止凭第一次快照继续应用或自愈擦写。 */
        if (((selected_status != AURORA_STORAGE_PAGE_VALID) &&
             (selected_status != AURORA_STORAGE_PAGE_VALID_LEGACY)) ||
            (selected_sequence != (choose_b ? seq_b : seq_a)))
        {
            runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_NONE;
            runtime->app.storage.write_inhibited = true;
            aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
            return;
        }

        runtime->app.storage.sequence = selected_sequence;
        runtime->app.storage.active_page =
            choose_b ? AURORA_STORAGE_ACTIVE_PAGE_B : AURORA_STORAGE_ACTIVE_PAGE_A;
        aurora_app_apply_settings(&runtime->app, &runtime->app.storage.settings, now_ms);

        runtime->app.storage.repair_pending = !valid_a || !valid_b ||
                                              status_a == AURORA_STORAGE_PAGE_VALID_LEGACY ||
                                              status_b == AURORA_STORAGE_PAGE_VALID_LEGACY;
        if (runtime->app.storage.repair_pending)
        {
            aurora_storage_mark_dirty(&runtime->app.storage, now_ms);
        }
        return;
    }

    if ((status_a == AURORA_STORAGE_PAGE_ERASED) && (status_b == AURORA_STORAGE_PAGE_ERASED))
    {
        runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_NONE;
        aurora_storage_mark_dirty(&runtime->app.storage, now_ms);
        return;
    }

    /* 两页均无可信记录时禁止本会话继续自动擦写，避免在未知配置上反复自愈。 */
    runtime->app.storage.active_page = AURORA_STORAGE_ACTIVE_NONE;
    runtime->app.storage.write_inhibited = true;
    aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
}

'''
replace_region("app/src/main.c", load_start, load_end, new_load)

write_tx_start = """/*---------------------------------------------------------------------------*\n * Name        : static aurora_status_t storage_write_transaction("""
write_tx_end = """/*---------------------------------------------------------------------------*\n * Name        : static void runtime_storage(aurora_runtime_t *runtime,"""
new_write_tx = r'''/*---------------------------------------------------------------------------*
 * Name        : static aurora_status_t storage_write_transaction(
 *               const aurora_storage_ctx_t *storage, uint32_t next_sequence,
 *               uint32_t target, uint8_t *page)
 * Input       : storage - 当前设置；next_sequence - 待提交序号；target - 固定备用页；
 *               page - 唯一静态512B工作页
 * Output      : OK成功；NOT_READY表示VDD失去资格需延后；INVALID/IO_ERROR表示真实事务错误
 * Description : 擦备用页→写正文→最后Commit→32B小块回读逐字节比对；避免完整page/settings验证副本占目标栈。
 *---------------------------------------------------------------------------*/
static aurora_status_t storage_write_transaction(const aurora_storage_ctx_t *storage,
                                                  uint32_t next_sequence, uint32_t target,
                                                  uint8_t *page)
{
    uint8_t verify[32];
    const uint32_t marker = AURORA_STORAGE_COMMIT_MARKER;
    size_t used;
    size_t offset;

    used = aurora_storage_encode_page_sequence(&storage->settings, next_sequence, page,
                                               AURORA_STORAGE_PAGE_SIZE, false);
    if (used < AURORA_STORAGE_HEADER_SIZE)
    {
        return AURORA_STATUS_INVALID;
    }
    if (!drv_system_flash_supply_is_safe())
    {
        return AURORA_STATUS_NOT_READY;
    }
    if (!drv_flash_erase_page(target))
    {
        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR
                                                 : AURORA_STATUS_NOT_READY;
    }
    if (!drv_system_flash_supply_is_safe())
    {
        return AURORA_STATUS_NOT_READY;
    }
    if (!drv_flash_program(target, page, AURORA_STORAGE_COMMIT_OFFSET))
    {
        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR
                                                 : AURORA_STATUS_NOT_READY;
    }
    if (!drv_system_flash_supply_is_safe())
    {
        return AURORA_STATUS_NOT_READY;
    }
    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t),
                           &page[AURORA_STORAGE_COMMIT_OFFSET + sizeof(uint32_t)],
                           used - AURORA_STORAGE_COMMIT_OFFSET - sizeof(uint32_t)))
    {
        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR
                                                 : AURORA_STATUS_NOT_READY;
    }
    if (!drv_system_flash_supply_is_safe())
    {
        return AURORA_STATUS_NOT_READY;
    }
    if (!drv_flash_program(target + AURORA_STORAGE_COMMIT_OFFSET, &marker, sizeof(marker)))
    {
        return drv_system_flash_supply_is_safe() ? AURORA_STATUS_IO_ERROR
                                                 : AURORA_STATUS_NOT_READY;
    }
    if (!drv_system_flash_supply_is_safe())
    {
        return AURORA_STATUS_NOT_READY;
    }

    /* 重新编码committed镜像作为期望值；不再分配448B verified_settings做整页解码。 */
    if (aurora_storage_encode_page_sequence(&storage->settings, next_sequence, page,
                                            AURORA_STORAGE_PAGE_SIZE, true) != used)
    {
        return AURORA_STATUS_INVALID;
    }

    for (offset = 0U; offset < AURORA_STORAGE_PAGE_SIZE; offset += sizeof(verify))
    {
        size_t chunk = AURORA_STORAGE_PAGE_SIZE - offset;
        if (chunk > sizeof(verify))
        {
            chunk = sizeof(verify);
        }
        if (!drv_flash_read(target + (uint32_t)offset, verify, chunk) ||
            (memcmp(verify, &page[offset], chunk) != 0))
        {
            return AURORA_STATUS_IO_ERROR;
        }
        if (!drv_system_flash_supply_is_safe())
        {
            return AURORA_STATUS_NOT_READY;
        }
    }
    return AURORA_STATUS_OK;
}

'''
replace_region("app/src/main.c", write_tx_start, write_tx_end, new_write_tx)

runtime_storage_start = """/*---------------------------------------------------------------------------*\n * Name        : static void runtime_storage(aurora_runtime_t *runtime,"""
runtime_storage_end = """/*---------------------------------------------------------------------------*\n * Name        : static void runtime_watchdog(aurora_runtime_t *runtime,"""
new_runtime_storage = r'''/*---------------------------------------------------------------------------*
 * Name        : static void runtime_storage(aurora_runtime_t *runtime,
 *               uint32_t now_ms)
 * Input       : runtime - 应用运行上下文；now_ms - 当前毫秒
 * Output      : 无
 * Description : 只在稳定停机+Relay/PWM物理关闭+VDD资格有效时写备用页；成功回读后才切换Journal提交状态。
 *---------------------------------------------------------------------------*/
static void runtime_storage(aurora_runtime_t *runtime, uint32_t now_ms)
{
    aurora_status_t status = AURORA_STATUS_IO_ERROR;
    uint32_t target;
    uint32_t next_sequence;
    uint8_t attempt;

    if (!runtime->app.storage.dirty || runtime->app.storage.write_inhibited ||
        ((now_ms - runtime->app.storage.dirty_since_ms) < AURORA_STORAGE_DIRTY_HOLD_MS) ||
        !storage_state_allows_write(runtime) ||
        ((now_ms - runtime->app.power_stage.state_since_ms) < AURORA_STORAGE_STOP_HOLD_MS) ||
        drv_pwm_output_active() || runtime->relay_applied)
    {
        return;
    }

    /* 欠压/棕断只延后保存；绝不把LVD/PVD事件当成一次“最后写Flash”的触发源。 */
    if (!drv_system_flash_supply_is_safe())
    {
        return;
    }

    target = storage_inactive_page(&runtime->app.storage);
    if (target == 0U)
    {
        runtime->app.storage.write_inhibited = true;
        aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
        return;
    }

    next_sequence = runtime->app.storage.sequence + 1U;
    for (attempt = 0U; attempt < AURORA_STORAGE_WRITE_ATTEMPTS; ++attempt)
    {
        status = storage_write_transaction(&runtime->app.storage, next_sequence, target,
                                           g_storage_page_workspace);
        if (status == AURORA_STATUS_OK)
        {
            runtime->app.storage.sequence = next_sequence;
            runtime->app.storage.active_page =
                (target == drv_board_flash_page_a()) ? AURORA_STORAGE_ACTIVE_PAGE_A
                                                     : AURORA_STORAGE_ACTIVE_PAGE_B;
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
            return;
        }
        if (status == AURORA_STATUS_NOT_READY)
        {
            /* VDD资格丢失时保留旧active page和dirty，等待下一次稳定停机窗口。 */
            return;
        }
        if (status == AURORA_STATUS_INVALID)
        {
            break;
        }
        /* IO_ERROR只允许在同一个target备用页上再试一次，绝不递增已提交sequence或切到active页。 */
    }

    runtime->app.storage.write_inhibited = true;
    aurora_protection_latch_fast_fault(&runtime->app.protection, AURORA_FAULT_STORAGE, now_ms);
}

'''
replace_region("app/src/main.c", runtime_storage_start, runtime_storage_end, new_runtime_storage)

# ---------------------------------------------------------------------------
# 3. PVD注释与现行职责一致：启动资格 + Flash写入veto；仍无IRQ/Reset。
# ---------------------------------------------------------------------------
replace_once(
    "driver/src/drv_system.c",
    """ * Description : 仅在PVD模块建立异常时关闭监测；正常启动后保留只读PVD供Flash写入veto使用。""",
    """ * Description : PVD用于启动供电资格并在正常启动后保留为只读Flash写入veto；始终不启用PVD IRQ/Reset。""",
)

# ---------------------------------------------------------------------------
# 4. 静态契约锁死1KB目标栈：禁止A/B整页、settings副本和staged ctx重新回栈。
# ---------------------------------------------------------------------------
contract = read("tests/test_flash_safe_persistence_contract.py")
old = '''    def test_driver_rejects_address_zero_cross_page_and_low_supply(self):\n'''
new = '''    def test_storage_workspace_stays_off_1k_target_stack(self):\n        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")\n        startup = (ROOT / "vendor/device/Source/startup_g32f031.s").read_text(encoding="utf-8")\n        self.assertIn("Stack_Size      EQU     0x00000400", startup)\n        self.assertIn("static uint8_t g_storage_page_workspace[AURORA_STORAGE_PAGE_SIZE]", main)\n        self.assertNotIn("uint8_t page_a[AURORA_STORAGE_PAGE_SIZE]", main)\n        self.assertNotIn("uint8_t page_b[AURORA_STORAGE_PAGE_SIZE]", main)\n        self.assertNotIn("aurora_persistent_settings_t settings_a", main)\n        self.assertNotIn("aurora_persistent_settings_t settings_b", main)\n        storage = main[main.index("static aurora_status_t storage_write_transaction"):\n                       main.index("static void runtime_watchdog")]\n        self.assertNotIn("aurora_storage_ctx_t staged", storage)\n        self.assertNotIn("aurora_persistent_settings_t verified_settings", storage)\n        self.assertIn("uint8_t verify[32]", storage)\n\n    def test_driver_rejects_address_zero_cross_page_and_low_supply(self):\n'''
if contract.count(old) != 1:
    raise SystemExit("flash persistence contract insertion point changed")
write("tests/test_flash_safe_persistence_contract.py", contract.replace(old, new, 1))

# ---------------------------------------------------------------------------
# 5. 文档记录启动栈失效链，避免后续只关注brownout而漏掉重启栈破坏。
# ---------------------------------------------------------------------------
doc = read("docs/48-v0.10.3-Flash安全持久化与最近三次远端提交审核.md")
append = r'''

## 7. 二次审核：1KB目标栈与启动Journal读取

目标 `startup_g32f031.s` 的 `Stack_Size` 只有 `0x400 = 1024B`。原主线 `load_storage()` 同时声明两张512B页缓冲和两份约448B持久化settings，理论局部数据已接近1.9KB；本轮第一版保存修复又曾在运行写路径叠加512B页、完整staged ctx和verified settings。Host CI使用桌面进程栈，无法自动暴露这一类Cortex-M0+目标栈问题。

最终修复不扩大目标栈，而是：

- A/B启动读取共用一个文件级静态512B Journal工作页；
- 第一轮只收集两页状态/序号，选中后再重读最终可信页，因此不需要同时保留两套settings；
- 运行写入也复用同一个静态页；
- 下一序号显式传给编码器，不复制完整`aurora_storage_ctx_t`；
- Commit后重新编码期望页，再用32B小块回读逐字节比较，不再创建完整verified settings副本；
- Python契约直接检查目标Stack_Size=1KB且这些大对象没有重新回到`main.c`调用栈。

这条修复与“演示模式运行后重启偶发无法运行、重烧恢复”的现象具有更直接的软件相关性：异常发生点就在重启后的Journal读取阶段。但没有目标HardFault栈水位/MAP或故障现场寄存器证据前，仍不能把历史现场唯一归因于该问题。
'''
if "## 7. 二次审核：1KB目标栈与启动Journal读取" not in doc:
    write("docs/48-v0.10.3-Flash安全持久化与最近三次远端提交审核.md", doc.rstrip() + append + "\n")

print("Flash Journal 1KB目标栈收尾修复已应用")
