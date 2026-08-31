from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class V0103Contracts(unittest.TestCase):
    def test_minimal_relay_holdoff_contract(self):
        types = (ROOT / "app/inc/app_types.h").read_text(encoding="utf-8")
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        power = (ROOT / "app/src/power_stage.c").read_text(encoding="utf-8")

        self.assertIn("AURORA_POWER_RELAY_HOLD_OFF", types)
        self.assertIn("AURORA_RELAY_PWM_OFF_DECAY_MS               (20U)", config)
        self.assertIn("relay_holdoff_sequence", power)
        self.assertIn("sample->sequence == ctx->relay_holdoff_sequence", power)
        self.assertIn("bool relay_applied", power)

    def test_break_uses_software_arm_state(self):
        main_h = (ROOT / "app/inc/main.h").read_text(encoding="utf-8")
        main_c = (ROOT / "app/src/main.c").read_text(encoding="utf-8")

        self.assertIn("volatile uint8_t pwm_arm_state", main_h)
        self.assertIn("AURORA_RUNTIME_PWM_ARM_WAIT_ZERO", main_h)
        self.assertIn("pwm_was_authorized", main_c)
        self.assertIn("runtime->pwm_arm_state == AURORA_RUNTIME_PWM_ARM_ACTIVE", main_c)
        comparator = main_c[main_c.index("void aurora_runtime_isr_comparator_fault"):]
        self.assertNotIn("if (!pwm_was_active)", comparator[:1800])

    def test_demo_cap_and_power_gates_remain_locked(self):
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        board = (ROOT / "driver/inc/board_config.h").read_text(encoding="utf-8")

        self.assertIn("AURORA_DEMO_HARD_POWER_CAP_MW               (30000U)", config)
        self.assertIn("AURORA_FW_VERSION_PATCH                     (3U)", config)
        for token in [
            "BOARD_GATE_COMP_ROUTE_VALIDATED             (0U)",
            "BOARD_GATE_ANALOG_CALIBRATED                (0U)",
            "BOARD_GATE_KEIL_LINKED                      (0U)",
            "BOARD_GATE_LOW_VOLTAGE_BENCH                (0U)",
            "BOARD_GATE_DEMO_LOAD_VALIDATED              (0U)",
            "BOARD_POWER_OUTPUT_ALLOWED                  (0U)",
        ]:
            self.assertIn(token, board)

    def test_ota_remains_out_of_scope(self):
        config = (ROOT / "app/inc/app_config.h").read_text(encoding="utf-8")
        self.assertNotIn("AURORA_OTA", config)
        self.assertNotIn("AURORA_IAP", config)


if __name__ == "__main__":
    unittest.main()
