from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FlashStorageSafetyContract(unittest.TestCase):
    def test_flash_driver_is_hard_bounded_to_final_two_pages(self):
        board = (ROOT / "driver/inc/board_config.h").read_text(encoding="utf-8")
        flash = (ROOT / "driver/src/drv_flash.c").read_text(encoding="utf-8")
        scatter = (ROOT / "project/AuroraControl.sct").read_text(encoding="utf-8")
        self.assertIn("BOARD_FLASH_TOTAL_SIZE_BYTES", board)
        self.assertIn("0x0000FC00UL", board)
        self.assertIn("0x0000FE00UL", board)
        self.assertIn("range_in_single_nvm_page", flash)
        self.assertIn("Flash Journal must occupy the final two physical pages only", flash)
        self.assertIn("LR_IROM1 0x00000000 0x0000FC00", scatter)

    def test_storage_layout_is_checked_at_compile_time(self):
        storage = (ROOT / "app/inc/storage.h").read_text(encoding="utf-8")
        self.assertIn("AURORA_STORAGE_ENCODED_SIZE", storage)
        self.assertIn("AURORA_STORAGE_PAYLOAD_USED_SIZE", storage)
        self.assertIn("Flash v3 payload layout does not match", storage)
        self.assertIn("Flash v3 encoded record exceeds one physical page", storage)

    def test_runtime_writes_only_in_stopped_states_and_bounds_retry(self):
        main = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        self.assertIn("runtime_storage_stop_state", main)
        for state in ("AURORA_POWER_WAIT_PV", "AURORA_POWER_NO_SUN", "AURORA_POWER_FAULT", "AURORA_POWER_OFF"):
            self.assertIn(state, main)
        self.assertIn("runtime_storage_program_page", main)
        self.assertIn("write_blocked", main)
        self.assertIn("AURORA_STORAGE_WRITE_RETRY_MAX", config)
        self.assertNotIn("drv_system_supply_is_good", main[main.index("static void runtime_storage"):main.index("static void runtime_watchdog")])

    def test_pvd_remains_boot_only_not_a_flash_save_trigger(self):
        system = (ROOT / "driver/src/drv_system.c").read_text(encoding="utf-8")
        self.assertIn("PVD is a boot qualifier, not a reset source", system)
        self.assertNotIn("drv_flash_program", system)
        self.assertNotIn("aurora_storage_mark_dirty", system)


if __name__ == "__main__":
    unittest.main()
