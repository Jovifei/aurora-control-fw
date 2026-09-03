#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).resolve().parent / "_flash_safe_stop_patch.py"
text = path.read_text(encoding="utf-8")
start_marker = '''replace_once(\n    "tests/test_v0103.c",\n    """    test_legacy_energy_fields_keep_charge_semantics();'''
end_marker = '''# Python静态契约锁死：不允许以后重新引入欠压写、pre-increment或跨页编程。'''
start = text.index(start_marker)
end = text.index(end_marker, start)
replacement = '''text = read("tests/test_v0103.c")\nmain_pos = text.index("int main(void)")\nprintf_pos = text.index("    printf(", main_pos)\nflash_calls = """    test_flash_low_supply_is_defer_only();\\n    test_flash_only_writes_in_stable_stop_state();\\n    test_flash_safe_stop_commits_and_reads_back();\\n    test_flash_retries_same_inactive_page_once();\\n    test_flash_double_failure_preserves_active_page();\\n    test_flash_driver_rejects_zero_and_cross_page();\\n"""\nwrite("tests/test_v0103.c", text[:printf_pos] + flash_calls + text[printf_pos:])\n\n'''
path.write_text(text[:start] + replacement + text[end:], encoding="utf-8", newline="\n")
print("已修正测试入口注入方式")
