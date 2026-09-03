from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FlashSafePersistenceContract(unittest.TestCase):
    def test_runtime_only_writes_in_stable_stop_with_supply_veto(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        self.assertIn("storage_state_allows_write", main)
        self.assertIn("AURORA_STORAGE_STOP_HOLD_MS", main)
        self.assertIn("drv_system_flash_supply_is_safe()", main)
        self.assertIn("AURORA_STORAGE_WRITE_ATTEMPTS", config)
        storage = main[main.index("static void runtime_storage"):main.index("static void runtime_watchdog")]
        self.assertNotIn("runtime->app.storage.sequence++;", storage)
        self.assertIn("staged.sequence = runtime->app.storage.sequence + 1U;", storage)
        self.assertIn("runtime->app.storage.write_inhibited = true;", storage)

    def test_driver_rejects_address_zero_cross_page_and_low_supply(self):
        flash = (ROOT / "driver/src/drv_flash.c").read_text(encoding="utf-8")
        self.assertIn("range_in_single_nvm_page", flash)
        self.assertIn("!drv_system_flash_supply_is_safe()", flash)
        self.assertIn("address >= first", flash)
        self.assertIn("end <= a_last", flash)
        self.assertIn("end <= b_last", flash)

    def test_pvd_is_veto_not_power_fail_write_trigger(self):
        system = (ROOT / "driver/src/drv_system.c").read_text(encoding="utf-8")
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        self.assertIn("bool drv_system_flash_supply_is_safe(void)", system)
        self.assertIn("DDL_PMU_DisableIT_PVD();", system)
        self.assertIn("DDL_RCC_Disable_PVDRST();", system)
        self.assertNotIn("AURORA_FAULT_PVD", main)
        self.assertNotIn("power_fail", main.lower())


if __name__ == "__main__":
    unittest.main()
