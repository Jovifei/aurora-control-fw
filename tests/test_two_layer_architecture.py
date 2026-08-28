import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class TwoLayerArchitectureTest(unittest.TestCase):
    """固定v0.8.3产品代码只能是APP→Driver→Vendor两层首方架构。"""

    def test_removed_service_and_board_layers_stay_removed(self) -> None:
        self.assertFalse((ROOT / "service").exists())
        self.assertFalse((ROOT / "board").exists())
        self.assertFalse((ROOT / "project/keil").exists())

    def test_expected_app_layout(self) -> None:
        self.assertTrue((ROOT / "app/inc/main.h").is_file())
        self.assertTrue((ROOT / "app/inc/debug.h").is_file())
        self.assertTrue((ROOT / "app/inc/interrupts.h").is_file())
        self.assertTrue((ROOT / "app/src/main.c").is_file())
        self.assertTrue((ROOT / "app/src/interrupts.c").is_file())
        self.assertTrue((ROOT / "app/src/debug.c").is_file())
        self.assertFalse((ROOT / "app/inc/app.h").exists())
        self.assertFalse((ROOT / "app/src/app.c").exists())

    def test_expected_driver_layout(self) -> None:
        for path in [
            "driver/inc/driver.h",
            "driver/inc/board_config.h",
            "driver/inc/drv_board.h",
            "driver/inc/drv_adc.h",
            "driver/inc/drv_comp.h",
            "driver/inc/drv_flash.h",
            "driver/inc/drv_io.h",
            "driver/inc/drv_pwm.h",
            "driver/inc/drv_system.h",
            "driver/inc/drv_uart.h",
            "driver/inc/drv_watchdog.h",
            "driver/src/drv_board.c",
        ]:
            self.assertTrue((ROOT / path).is_file(), path)

    def test_app_uses_driver_contract_without_vendor_headers(self) -> None:
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        interrupts = (ROOT / "app/src/interrupts.c").read_text(encoding="utf-8")
        self.assertIn('#include "driver.h"', main)
        self.assertIn('#include "driver.h"', interrupts)
        for text in [main, interrupts]:
            self.assertNotRegex(text, r'#include\s+"g32')
            self.assertNotIn("DDL_", text)
            self.assertNotIn("GPIOA", text)
            self.assertNotIn("ATMR->", text)

    def test_driver_never_includes_application_code(self) -> None:
        app_headers = {
            "main.h", "app_config.h", "app_types.h", "debug.h", "interrupts.h",
            "charger.h", "measurement.h", "mppt.h", "power_stage.h",
            "protection.h", "protocol.h", "storage.h", "ui.h",
        }
        include_re = re.compile(r'#include\s+"([^"]+)"')
        for path in sorted((ROOT / "driver").rglob("*.[ch]")):
            text = path.read_text(encoding="utf-8")
            included = {match.group(1) for match in include_re.finditer(text)}
            self.assertFalse(included & app_headers, f"{path}: {included & app_headers}")

    def test_project_files_are_flat(self) -> None:
        self.assertTrue((ROOT / "project/AuroraControl.uvprojx").is_file())
        self.assertTrue((ROOT / "project/AuroraControl.sct").is_file())
        self.assertTrue((ROOT / "project/README.md").is_file())


if __name__ == "__main__":
    unittest.main()
