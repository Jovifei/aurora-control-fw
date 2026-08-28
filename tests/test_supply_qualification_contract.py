import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SupplyQualificationContractTest(unittest.TestCase):
    """约束v0.8.1弱光启动资格，防止后续重构重新引入复位循环或过早初始化。"""

    def setUp(self) -> None:
        self.main = (ROOT / "project/keil/main.c").read_text(encoding="utf-8")
        self.driver = (ROOT / "driver/src/drv_system.c").read_text(encoding="utf-8")
        self.config = (ROOT / "board/board_config.h").read_text(encoding="utf-8")

    def test_default_parameters_are_explicit_and_changeable(self) -> None:
        expected = {
            "BOARD_MCU_PVD_THRESHOLD_MV": "2800UL",
            "BOARD_MCU_PVD_FILTER_US": "50UL",
            "BOARD_MCU_PVD_READY_TIMEOUT_US": "1000UL",
            "BOARD_MCU_SUPPLY_STABLE_TIME_MS": "100UL",
            "BOARD_MCU_SUPPLY_CHECK_PERIOD_MS": "1UL",
            "BOARD_MCU_PVD_RESET_ENABLE": "0U",
            "BOARD_MCU_PVD_IRQ_ENABLE": "0U",
        }
        for name, value in expected.items():
            self.assertRegex(
                self.config,
                rf"#define\s+{name}\s+\({re.escape(value)}\)",
                msg=f"missing or changed default: {name}",
            )

    def test_pvd_is_boot_qualifier_not_reset_or_irq_fault(self) -> None:
        self.assertIn("DDL_RCC_HSI_IsReady()", self.driver)
        self.assertIn("DDL_RCC_Disable_PVDRST();", self.driver)
        self.assertIn("DDL_PMU_DisableIT_PVD();", self.driver)
        self.assertIn("DDL_PMU_EnablePVD();", self.driver)
        self.assertIn("DDL_PMU_IsActiveFlag_PVDRDY()", self.driver)
        self.assertIn("DDL_PMU_GetPVDMonitoringResult()", self.driver)
        self.assertIn("#error \"BOARD_MCU_PVD_RESET_ENABLE must remain 0", self.driver)
        self.assertIn("#error \"BOARD_MCU_PVD_IRQ_ENABLE must remain 0", self.driver)
        self.assertNotIn("DDL_RCC_Enable_PVDRST();", self.driver)
        self.assertNotIn("DDL_PMU_EnableIT_PVD();", self.driver)

    def test_full_initialization_occurs_only_after_supply_qualification(self) -> None:
        system_pos = self.main.index("drv_system_init();")
        gpio_pos = self.main.index("drv_io_init();")
        wait_pos = self.main.index("if (!drv_system_wait_for_supply_stable())")
        service_pos = self.main.index("if (!aurora_service_init(&g_aurora_service))")

        self.assertLess(system_pos, gpio_pos)
        self.assertLess(gpio_pos, wait_pos)
        self.assertLess(wait_pos, service_pos)
        self.assertNotIn("drv_watchdog_init", self.main[:service_pos])
        self.assertNotIn("drv_pwm_init", self.main[:service_pos])
        self.assertNotIn("drv_comp_init", self.main[:service_pos])
        self.assertNotIn("drv_adc_init", self.main[:service_pos])

    def test_unstable_vdd_restarts_continuous_stability_window(self) -> None:
        self.assertIn("stable_tracking = false;", self.driver)
        self.assertIn("stable_start_ms = 0U;", self.driver)
        self.assertIn("BOARD_MCU_SUPPLY_STABLE_TIME_MS", self.driver)
        self.assertIn("BOARD_MCU_SUPPLY_CHECK_PERIOD_MS", self.driver)
        self.assertIn("__WFI();", self.driver)

    def test_runtime_application_has_no_pvd_fault_category(self) -> None:
        app_text = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "app").rglob("*.[ch]"))
        )
        self.assertNotIn("AURORA_FAULT_PVD", app_text)
        self.assertNotIn("MCU_LOW_VOLTAGE", app_text)
        self.assertNotIn("PVDRSTEN", app_text)


if __name__ == "__main__":
    unittest.main()
