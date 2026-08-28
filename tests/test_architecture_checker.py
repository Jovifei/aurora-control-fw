import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ArchitectureCheckerTests(unittest.TestCase):
    def test_git_metadata_is_not_scanned_as_project_content(self):
        result = subprocess.run(
            [sys.executable, "tools/check_architecture.py"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(".git", result.stdout)

    def test_keil_external_sources_keep_intermediate_files_in_output_dir(self):
        project = ET.parse(ROOT / "project" / "AuroraControl.uvprojx")
        for file_node in project.findall(".//File"):
            file_path = file_node.findtext("FilePath", default="")
            file_name = file_node.findtext("FileName", default="")
            if file_path.startswith("..\\"):
                self.assertNotIn("..", file_name)

    def test_product_layers_are_split_into_app_and_driver(self):
        self.assertFalse((ROOT / "project" / "keil").exists())
        self.assertFalse((ROOT / "service").exists())
        self.assertFalse((ROOT / "board").exists())
        for name in ["main", "interrupts", "debug"]:
            self.assertTrue((ROOT / "app" / "src" / f"{name}.c").is_file())
            self.assertTrue((ROOT / "app" / "inc" / f"{name}.h").is_file())
        self.assertTrue((ROOT / "driver" / "src" / "drv_board.c").is_file())
        self.assertTrue((ROOT / "driver" / "inc" / "drv_board.h").is_file())
        self.assertTrue((ROOT / "project" / "AuroraControl.uvprojx").is_file())
        self.assertTrue((ROOT / "project" / "AuroraControl.sct").is_file())
        self.assertTrue((ROOT / "project" / "README.md").is_file())

        for source in [*(ROOT / "app" / "src").glob("*.c"),
                       *(ROOT / "driver" / "src").glob("*.c")]:
            self.assertTrue(
                (source.parent.parent / "inc" / f"{source.stem}.h").is_file(),
                str(source),
            )

    def test_usart_declares_bluetooth_default_and_debug_route(self):
        board_config = (ROOT / "driver" / "inc" / "board_config.h").read_text(encoding="utf-8")
        uart = (ROOT / "driver" / "src" / "drv_uart.c").read_text(encoding="utf-8")
        self.assertIn("BOARD_USART_MODE_BLUETOOTH", board_config)
        self.assertIn("BOARD_USART_MODE_DEBUG", board_config)
        self.assertIn("BOARD_USART_MODE", board_config)
        self.assertRegex(
            board_config,
            r"#define\s+BOARD_USART_MODE\s+BOARD_USART_MODE_BLUETOOTH",
        )
        for token in [
            "BOARD_PIN_UART_TX_NUMBER",
            "BOARD_PIN_UART_RX_NUMBER",
            "BOARD_PIN_DEBUG_TX_NUMBER",
            "BOARD_PIN_DEBUG_RX_NUMBER",
            "DRV_UART_GPIO_PIN",
        ]:
            self.assertIn(token, uart)

        project = ET.parse(ROOT / "project" / "AuroraControl.uvprojx")
        paths = {
            file_node.findtext("FilePath", default="")
            for file_node in project.findall(".//File")
        }
        self.assertIn("..\\app\\src\\main.c", paths)
        self.assertIn("..\\app\\src\\interrupts.c", paths)


if __name__ == "__main__":
    unittest.main()
