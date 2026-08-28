from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class V090Contracts(unittest.TestCase):
    def test_board_current_model_is_3v3_unbiased(self):
        text = (ROOT / "driver/inc/board_config.h").read_text(encoding="utf-8")
        self.assertIn("BOARD_ADC_REFERENCE_MV                      (3300L)", text)
        self.assertIn("BOARD_ADC_PV_I_ZERO_CODE                    (0)", text)
        self.assertIn("BOARD_ADC_PV_I_POLARITY                     (1)", text)
        self.assertIn("85.8V", text)
        self.assertIn("BOARD_POWER_OUTPUT_ALLOWED                  (0U)", text)

    def test_ntc_direction_and_ratio_contract(self):
        cfg = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        meas = (ROOT / "app/src/measurement.c").read_text(encoding="utf-8")
        self.assertIn("AURORA_NTC_PULL_OHM", cfg)
        self.assertIn("AURORA_NTC_OPEN_RAW_MIN", cfg)
        self.assertIn("AURORA_NTC_SHORT_RAW_MAX", cfg)
        self.assertIn("AURORA_ZERO_CAL_MAX_ATTEMPT_BLOCKS", cfg)
        self.assertIn("AURORA_PV_START_QUALIFY_MS", cfg)
        self.assertIn("raw >= AURORA_NTC_OPEN_RAW_MIN", meas)
        self.assertIn("raw <= AURORA_NTC_SHORT_RAW_MAX", meas)
        self.assertNotIn("AURORA_NTC_OPEN_TEMP_DC", cfg)

    def test_mature_charge_timings_exist(self):
        cfg = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        for token in [
            "AURORA_TRICKLE_TO_CC_HOLD_MS",
            "AURORA_CC_TO_CV_SCORE_THRESHOLD",
            "AURORA_CV_TO_CC_HOLD_MS",
            "AURORA_RECHARGE_HOLD_MS",
            "AURORA_FLOAT_ENTRY_HOLD_MS",
            "AURORA_FLOAT_END_HOLD_MS",
            "AURORA_FLOAT_LOW_VOLT_HOLD_MS",
        ]:
            self.assertIn(token, cfg)

    def test_cv_return_requires_control_stability(self):
        charger = (ROOT / "app/src/charger.c").read_text(encoding="utf-8")
        marker = "case AURORA_CHARGE_CV:"
        start = charger.index(marker)
        window = charger[start:start + 1800]
        self.assertIn("!weak_light && !thermal_limited && !input_limited", window)
        self.assertIn("AURORA_CHARGER_CV_RETURN_HYST_MV", window)
        self.assertIn("AURORA_CV_TO_CC_HOLD_MS", window)

    def test_recharge_and_float_low_voltage_request_requalification(self):
        types = (ROOT / "app/inc/app_types.h").read_text(encoding="utf-8")
        charger = (ROOT / "app/src/charger.c").read_text(encoding="utf-8")
        power = (ROOT / "app/src/power_stage.c").read_text(encoding="utf-8")
        mppt = (ROOT / "app/src/mppt.c").read_text(encoding="utf-8")

        self.assertIn("bool restart_required", types)
        self.assertGreaterEqual(charger.count("request_restart(ctx, now_ms);"), 2)
        self.assertIn("output.restart_required = ctx->restart_required", charger)
        self.assertIn("if (ctx->restart_required)", charger)
        self.assertIn("if (charger->restart_required)", power)
        self.assertIn("enter_state(ctx, AURORA_POWER_BAT_STABILITY, now_ms);", power)
        self.assertIn("if (power_allow_mw == 0U)", mppt)
        self.assertIn("aurora_mppt_reset(ctx);", mppt)

    def test_bus_saturation_is_fail_safe(self):
        meas = (ROOT / "app/src/measurement.c").read_text(encoding="utf-8")
        prot = (ROOT / "app/src/protection.c").read_text(encoding="utf-8")
        self.assertIn("AURORA_MEAS_DIAG_BUS_ADC_SATURATED", meas)
        self.assertIn("AURORA_FAULT_BUS_ADC_SATURATION", prot)
        self.assertIn("pv_current_calibrated && !boost_output_active", prot)
        # Saturated BUS must not be marked valid.
        sat = meas.index("AURORA_MEAS_DIAG_BUS_ADC_SATURATED")
        window = meas[max(0, sat - 500):sat + 500]
        self.assertIn("AURORA_MEAS_VALID_BUS_V", window)

    def test_no_regression_of_relay_and_power_gate(self):
        power = (ROOT / "app/src/power_stage.c").read_text(encoding="utf-8")
        runtime = (ROOT / "app/src/main.c").read_text(encoding="utf-8")
        self.assertIn("AURORA_RELAY_CLOSE_DELTA_MV", power)
        self.assertIn("relay_close_still_safe", runtime)
        self.assertIn("drv_board_power_gate_open", runtime)


if __name__ == "__main__":
    unittest.main()