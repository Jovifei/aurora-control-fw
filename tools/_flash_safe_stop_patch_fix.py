#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).resolve().parent / "_flash_safe_stop_patch.py"
text = path.read_text(encoding="utf-8")

# 将test_v0103入口改为按main中的printf位置插入，避免依赖旧测试函数命名。
start_marker = '''replace_once(\n    "tests/test_v0103.c",\n    """    test_legacy_energy_fields_keep_charge_semantics();'''
end_marker = '''# Python静态契约锁死：不允许以后重新引入欠压写、pre-increment或跨页编程。'''
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = '''text = read("tests/test_v0103.c")\nmain_pos = text.index("int main(void)")\nprintf_pos = text.index("    printf(", main_pos)\nflash_calls = """    test_flash_low_supply_is_defer_only();\\n    test_flash_only_writes_in_stable_stop_state();\\n    test_flash_safe_stop_commits_and_reads_back();\\n    test_flash_retries_same_inactive_page_once();\\n    test_flash_double_failure_preserves_active_page();\\n    test_flash_driver_rejects_zero_and_cross_page();\\n"""\nwrite("tests/test_v0103.c", text[:printf_pos] + flash_calls + text[printf_pos:])\n\n'''
text = text[:start] + replacement + text[end:]

# 项目代码规范要求每个配置宏都有直接用途注释。
old_macros = '''#define AURORA_STORAGE_ACTIVE_NONE                  ((aurora_storage_active_page_t)0U)\\n#define AURORA_STORAGE_ACTIVE_PAGE_A                ((aurora_storage_active_page_t)1U)\\n#define AURORA_STORAGE_ACTIVE_PAGE_B                ((aurora_storage_active_page_t)2U)'''
new_macros = '''/* 尚无已提交可信页。 */\\n#define AURORA_STORAGE_ACTIVE_NONE                  ((aurora_storage_active_page_t)0U)\\n/* A页是当前已提交可信页。 */\\n#define AURORA_STORAGE_ACTIVE_PAGE_A                ((aurora_storage_active_page_t)1U)\\n/* B页是当前已提交可信页。 */\\n#define AURORA_STORAGE_ACTIVE_PAGE_B                ((aurora_storage_active_page_t)2U)'''
if old_macros not in text:
    raise SystemExit("active page macro block not found in patch script")
text = text.replace(old_macros, new_macros, 1)

# Host 64位平台上sizeof返回size_t；显式收窄到uint32_t，保持-Wconversion/-Werror严格门禁。
old_cross = '''drv_board_flash_page_a() + AURORA_STORAGE_PAGE_SIZE - sizeof(uint32_t);'''
new_cross = '''drv_board_flash_page_a() + AURORA_STORAGE_PAGE_SIZE - (uint32_t)sizeof(uint32_t);'''
if old_cross not in text:
    raise SystemExit("cross-page test expression not found in patch script")
text = text.replace(old_cross, new_cross, 1)

path.write_text(text, encoding="utf-8", newline="\n")
print("已修正测试入口、active页宏注释和严格类型转换")
