from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TargetRamContract(unittest.TestCase):
    def test_ram_gate_is_permanent_and_conservative(self):
        run_checks = (ROOT / "tools/run_checks.py").read_text(encoding="utf-8")
        ram_check = (ROOT / "tools/check_target_ram.py").read_text(encoding="utf-8")
        self.assertIn('run([sys.executable, "tools/check_target_ram.py"])', run_checks)
        self.assertIn("RAM_TOTAL_BYTES = 8192", ram_check)
        self.assertIn("MIN_ESTIMATED_FREE_BYTES = 2048", ram_check)
        self.assertIn('KEIL_RELEASE_OPTIMIZATION = "-O1"', ram_check)
        self.assertIn("Stack_Size", ram_check)
        self.assertIn("Heap_Size", ram_check)
        self.assertIn("project.findall(\".//FilePath\")", ram_check)

    def test_keil_release_optimization_is_level_one(self):
        project = (ROOT / "project/AuroraControl.uvprojx").read_text(encoding="utf-8")
        self.assertIn("<Optim>1</Optim>", project)
        self.assertNotIn("<Optim>0</Optim>", project)


if __name__ == "__main__":
    unittest.main()
