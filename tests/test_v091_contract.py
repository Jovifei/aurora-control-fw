from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class V091CloseoutContracts(unittest.TestCase):
    def test_all_mcu_analog_domain_is_3v3_and_ntc_models_match(self):
        board = (ROOT / "driver/inc/board_config.h").read_text(encoding="utf-8")
        app = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        self.assertIn("BOARD_ADC_REFERENCE_MV                      (3300L)", board)
        for token in [
            "BOARD_NTC_MOS_PULLUP_OHM                    (5100L)",
            "BOARD_NTC_MOS_R25_OHM                       (100000L)",
            "BOARD_NTC_MOS_BETA_KELVIN                   (3950L)",
            "BOARD_NTC_AMB_PULLUP_OHM                    (5100L)",
            "BOARD_NTC_AMB_R25_OHM                       (100000L)",
            "BOARD_NTC_AMB_BETA_KELVIN                   (3950L)",
        ]:
            self.assertIn(token, board)
        self.assertIn("AURORA_NTC_R25_OHM", app)
        self.assertIn("AURORA_NTC_BETA_KELVIN", app)
        self.assertIn("BOARD_POWER_OUTPUT_ALLOWED                  (0U)", board)

    def test_ntc_ratio_is_supply_independent(self):
        r_ntc = 100000.0
        r_pull = 5100.0
        ratio = r_ntc / (r_ntc + r_pull)
        raw_5v = 4095.0 * ((5.0 * ratio) / 5.0)
        raw_3v3 = 4095.0 * ((3.3 * ratio) / 3.3)
        self.assertAlmostEqual(raw_5v, raw_3v3, places=9)
        self.assertAlmostEqual(raw_3v3, 3896.29, places=1)

    def test_remaining_software_parity_contracts_are_present(self):
        storage_h = (ROOT / "app/inc/storage.h").read_text(encoding="utf-8")
        storage_c = (ROOT / "app/src/storage.c").read_text(encoding="utf-8")
        main_c = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        tests_c = (ROOT / "tests/test_main.c").read_text(encoding="utf-8")
        self.assertIn("AURORA_ENERGY_HISTORY_POINT_COUNT", storage_h)
        self.assertIn("AURORA_STORAGE_PAGE_CRC_ERROR", storage_h)
        self.assertIn("aurora_storage_classify_page", storage_c)
        self.assertIn("repair_pending", main_c)
        self.assertIn("AURORA_ENERGY_HISTORY_INTERVAL_MS", main_c)
        self.assertIn("test_v091_mppt_cloud_and_start_failure_vectors", tests_c)


if __name__ == "__main__":
    unittest.main()
