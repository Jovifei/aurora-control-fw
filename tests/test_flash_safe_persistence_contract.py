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

if __name__ == "__main__":
    unittest.main()
