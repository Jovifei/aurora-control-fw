from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeStackContract(unittest.TestCase):
    def test_protocol_workspaces_are_file_static(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        self.assertIn("static runtime_protocol_workspace_t g_protocol_workspace;", main)
        self.assertIn("aurora_protocol_take_frame(&runtime->app.protocol, &g_protocol_workspace.request)", main)
        self.assertIn("&g_protocol_workspace.tx_frame", main)
        self.assertIn("g_protocol_workspace.wire", main)

    def test_large_protocol_buffers_are_not_local_to_runtime_path(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        uart_start = main.index("static void process_uart(")
        uart_end = main.index("static aurora_storage_page_status_t storage_read_page(", uart_start)
        uart = main[uart_start:uart_end]
        poll_start = main.index("void aurora_runtime_poll(")
        poll_end = main.index("void aurora_runtime_isr_tick(", poll_start)
        poll = main[poll_start:poll_end]
        for body in (uart, poll):
            self.assertIsNone(re.search(r"aurora_protocol_frame_t\\s+\\w+\\s*;", body))
            self.assertNotIn("uint8_t wire[AURORA_PROTOCOL_MAX_WIRE]", body)

    def test_target_stack_gate_is_permanent(self):
        run_checks = (ROOT / "tools/run_checks.py").read_text(encoding="utf-8")
        stack_check = (ROOT / "tools/check_target_stack.py").read_text(encoding="utf-8")
        self.assertIn('run([sys.executable, "tools/check_target_stack.py"])', run_checks)
        self.assertIn("TARGET_STACK_BYTES = 1024", stack_check)
        self.assertIn("EXTRA_MARGIN_BYTES = 128", stack_check)
        self.assertIn("MAX_RUNTIME_POLL_FRAME = 256", stack_check)
        self.assertIn("nested IRQ reserve", stack_check)


if __name__ == "__main__":
    unittest.main()
