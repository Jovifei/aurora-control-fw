import subprocess
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class ArchitectureCheckerTests(unittest.TestCase):
    def test_git_metadata_is_not_scanned_as_project_content(self):
        result = subprocess.run(
            [sys.executable, "tools/check_architecture.py"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn(".git", result.stdout)

    def test_keil_external_sources_keep_intermediate_files_in_output_dir(self):
        project = ET.parse(ROOT / "project" / "AuroraControl.uvprojx")
        for file_node in project.findall(".//File"):
            file_path = file_node.findtext("FilePath", default="")
            file_name = file_node.findtext("FileName", default="")
            if file_path.startswith("..\\"):
                self.assertNotIn("..", file_name)

    def test_keil_does_not_reference_removed_layers(self):
        text = (ROOT / "project" / "AuroraControl.uvprojx").read_text(encoding="utf-8")
        self.assertNotIn("service\\", text)
        self.assertNotIn("board\\board.c", text)
        self.assertNotIn("app\\src\\app.c", text)
        self.assertIn("app\\src\\main.c", text)
        self.assertIn("driver\\src\\drv_board.c", text)


if __name__ == "__main__":
    unittest.main()
