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
        self.assertIn("next_sequence = runtime->app.storage.sequence + 1U;", storage)
        self.assertIn("runtime->app.storage.write_inhibited = true;", storage)

    def test_storage_workspace_stays_off_1k_target_stack(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        startup = (ROOT / "vendor/device/Source/startup_g32f031.s").read_text(encoding="utf-8")
        self.assertIn("Stack_Size      EQU     0x00000400", startup)
        self.assertIn("static uint8_t g_storage_page_workspace[AURORA_STORAGE_PAGE_SIZE]", main)
        self.assertNotIn("uint8_t page_a[AURORA_STORAGE_PAGE_SIZE]", main)
        self.assertNotIn("uint8_t page_b[AURORA_STORAGE_PAGE_SIZE]", main)
        self.assertNotIn("aurora_persistent_settings_t settings_a", main)
        self.assertNotIn("aurora_persistent_settings_t settings_b", main)
        storage = main[main.index("static aurora_status_t storage_write_transaction"):
                       main.index("static void runtime_watchdog")]
        self.assertNotIn("aurora_storage_ctx_t staged", storage)
        self.assertNotIn("aurora_persistent_settings_t verified_settings", storage)
        self.assertIn("uint8_t verify[32]", storage)

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
