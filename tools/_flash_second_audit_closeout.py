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


def replace_function(path: str, signature: str, replacement: str) -> None:
    text = read(path)
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    end = None
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                break
    if end is None:
        raise SystemExit(f"{path}: function end not found for {signature}")
    write(path, text[:start] + replacement.rstrip() + text[end:])


# 1. IWDT nominal clock follows the official 32.768 kHz LSI typical value.
replace_once(
    "driver/inc/board_config.h",
    """/* IWDT名义低速时钟，Hz；实板受LSI容差影响。 */\n#define BOARD_WATCHDOG_CLOCK_HZ                     (40000UL)""",
    """/* IWDT名义低速时钟，Hz；数据手册典型LSICLK为32.768kHz，实板仍受31~34kHz/温漂容差影响。 */\n#define BOARD_WATCHDOG_CLOCK_HZ                     (32768UL)""",
)
replace_once(
    "driver/src/drv_watchdog.c",
    "/* IWDT典型时钟按40kHz估算，分频64；实板需测量真实复位时间。 */",
    "/* IWDT按数据手册典型32.768kHz估算、分频64；实板仍需测量LSI容差下的真实复位时间。 */",
)

# 2. Remove full persistent-settings copies from the 1 KiB target call stack.
new_apply_settings = r'''void aurora_app_apply_settings(aurora_app_t *app, const aurora_persistent_settings_t *settings,
                               uint32_t now_ms)
{
    bool demo_power_clamped;

    if ((app == NULL) || (settings == NULL) || (settings->chemistry >= AURORA_CHEM_COUNT) ||
        (settings->pack >= AURORA_PACK_COUNT) || (settings->operating_mode >= AURORA_MODE_COUNT) ||
        (settings->demo_target_voltage_mv == 0U) ||
        (settings->demo_target_voltage_mv > AURORA_DEMO_MAX_TARGET_VOLTAGE_MV) ||
        (settings->demo_power_limit_mw == 0U) ||
        (settings->demo_power_limit_mw > AURORA_RATED_POWER_MW))
    {
        return;
    }

    demo_power_clamped = settings->demo_power_limit_mw > AURORA_DEMO_HARD_POWER_CAP_MW;
    if (settings != &app->storage.settings)
    {
        app->storage.settings = *settings;
    }
    if (demo_power_clamped)
    {
        app->storage.settings.demo_power_limit_mw = AURORA_DEMO_HARD_POWER_CAP_MW;
    }

    aurora_storage_energy_history_update(&app->storage.settings);
    app->energy_accumulator_mw_ms = app->storage.settings.pv_energy_remainder_mw_ms;
    app->charge_energy_accumulator_mw_ms = app->storage.settings.charge_est_energy_remainder_mw_ms;
    app->last_energy_history_ms = now_ms;
    aurora_charger_init(&app->charger, app->storage.settings.chemistry, app->storage.settings.pack,
                        now_ms);
    aurora_mppt_reset(&app->mppt);
    aurora_power_stage_init(&app->power_stage, now_ms);
    aurora_measurement_zero_cal_reset(&app->measurement);
    memset(&app->charge_output, 0, sizeof(app->charge_output));
    memset(&app->mppt_output, 0, sizeof(app->mppt_output));
    memset(&app->power_command, 0, sizeof(app->power_command));
    app->link_request = false;
    app->actual_power_transfer = false;
    app->last_energy_sample_sequence = 0U;
    app->last_energy_sample_timestamp_ms = 0U;
    app->relay_applied_feedback = false;

    if (demo_power_clamped)
    {
        aurora_storage_mark_dirty(&app->storage, now_ms);
    }
}'''
replace_function(
    "app/src/main.c",
    "void aurora_app_apply_settings(aurora_app_t *app, const aurora_persistent_settings_t *settings,",
    new_apply_settings,
)

replace_once(
    "app/src/main.c",
    """            aurora_persistent_settings_t settings = app->storage.settings;\n            settings.chemistry = (aurora_battery_chem_t)frame->data[0];\n            settings.pack = (aurora_battery_pack_t)frame->data[1];\n            settings.settings_revision++;\n            aurora_app_apply_settings(app, &settings, now_ms);""",
    """            app->storage.settings.chemistry = (aurora_battery_chem_t)frame->data[0];\n            app->storage.settings.pack = (aurora_battery_pack_t)frame->data[1];\n            app->storage.settings.settings_revision++;\n            aurora_app_apply_settings(app, &app->storage.settings, now_ms);""",
)
replace_once(
    "app/src/main.c",
    """            aurora_persistent_settings_t settings = app->storage.settings;\n            settings.operating_mode = (aurora_operating_mode_t)frame->data[0];\n            settings.settings_revision++;\n            aurora_app_apply_settings(app, &settings, now_ms);""",
    """            app->storage.settings.operating_mode = (aurora_operating_mode_t)frame->data[0];\n            app->storage.settings.settings_revision++;\n            aurora_app_apply_settings(app, &app->storage.settings, now_ms);""",
)
replace_once(
    "app/src/main.c",
    """                aurora_persistent_settings_t settings = app->storage.settings;\n                settings.demo_target_voltage_mv = voltage_mv;\n                settings.demo_power_limit_mw = power_mw;\n                settings.settings_revision++;\n                aurora_app_apply_settings(app, &settings, now_ms);""",
    """                app->storage.settings.demo_target_voltage_mv = voltage_mv;\n                app->storage.settings.demo_power_limit_mw = power_mw;\n                app->storage.settings.settings_revision++;\n                aurora_app_apply_settings(app, &app->storage.settings, now_ms);""",
)

# 3. Narrow brownout exposure: re-check supply before every 32-bit program operation.
new_drv_flash_program = r'''bool drv_flash_program(uint32_t address, const void *data, size_t length)
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
}'''
replace_function(
    "driver/src/drv_flash.c",
    "bool drv_flash_program(uint32_t address, const void *data, size_t length)",
    new_drv_flash_program,
)

# 4. Strengthen static contracts so these target-only regressions cannot silently return.
contract = read("tests/test_flash_safe_persistence_contract.py")
insert_before = "\n\nif __name__ == \"__main__\":\n"
if insert_before not in contract:
    raise SystemExit("contract insertion point not found")
new_tests = r'''
    def test_protocol_setting_updates_do_not_copy_full_persistent_struct_on_1k_stack(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        self.assertNotIn("aurora_persistent_settings_t sanitized;", main)
        self.assertNotIn("aurora_persistent_settings_t settings = app->storage.settings;", main)
        self.assertIn("aurora_app_apply_settings(app, &app->storage.settings, now_ms);", main)

    def test_watchdog_uses_official_lsi_typical_frequency(self):
        board = (ROOT / "driver/inc/board_config.h").read_text(encoding="utf-8")
        watchdog = (ROOT / "driver/src/drv_watchdog.c").read_text(encoding="utf-8")
        self.assertIn("#define BOARD_WATCHDOG_CLOCK_HZ                     (32768UL)", board)
        self.assertIn("32.768kHz", watchdog)

    def test_flash_program_rechecks_supply_before_each_word(self):
        flash = (ROOT / "driver/src/drv_flash.c").read_text(encoding="utf-8")
        program = flash[flash.index("bool drv_flash_program"):]
        self.assertIn("for (offset = 0U; offset < length; offset += sizeof(uint32_t))", program)
        self.assertIn("if (!drv_system_flash_supply_is_safe())", program)
        self.assertIn("DDL_FLASH_Write(address + (uint32_t)offset", program)
'''
contract = contract.replace(insert_before, "\n" + new_tests.rstrip() + insert_before, 1)
write("tests/test_flash_safe_persistence_contract.py", contract)

# 5. Record the second-pass findings and hardware boundary in the audit document.
doc_path = "docs/48-v0.10.3-Flash安全持久化与最近三次远端提交审核.md"
doc = read(doc_path).rstrip()
appendix = r'''

## 二次目标机审核补强

在永久CI首次通过后继续按G32F031真实资源约束复核，又关闭三项仅Host环境不容易暴露的风险：

1. **协议设置写入的嵌套栈峰值**：目标启动文件栈固定为1024B；旧路径 `process_uart()` 的请求/应答/线缓冲叠加协议层完整 `aurora_persistent_settings_t` 副本，再进入 `aurora_app_apply_settings()` 的第二份完整副本，理论峰值超过1KB。现改为直接更新已经校验的 `app->storage.settings` 字段，`apply_settings` 仅在输入不是当前存储对象时才复制一次，协议路径不再创建完整设置局部变量。
2. **IWDT名义时钟错误**：板级常量由40kHz改为数据手册典型LSICLK 32.768kHz；1s监督目标因此回到接近1s的名义值。实板仍必须覆盖LSI容差测量真实复位时间。
3. **长块Flash编程的欠压窗口**：Driver不再一次连续编程整个几百字节块，而是每32-bit word前重新读取PVD资格。检测到VDD资格丢失后立即停止启动后续word。512B页擦除仍是硬件不可分割操作，必须做实板掉电斜率和擦除阶段断电注入。

G32F031数据手册给出的典型Flash参数为：32-bit编程约60us、512B页擦除约2.45ms、Flash编程电压下限2.0V。当前2.8V PVD档的下降阈值规格约2.64~2.77V，因此软件具备静态电压裕量，但该结论不等价于板级保持时间已验证。
'''
write(doc_path, doc + appendix + "\n")

print("Flash/目标栈二次审核修复已应用")
