"""Portable CLI contract tests; actual source application is a separate gate."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/verify-reboot-integration.py"


class IntegrationTests(unittest.TestCase):
    def test_inventory_works_outside_repository_and_disclaims_application(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(SCRIPT), "--inventory-only"],
                                    cwd=directory, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("patch application NOT checked", result.stdout)

    def test_source_must_be_explicit_and_missing_tree_fails(self):
        result = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(SCRIPT), "--xnu-tree", directory],
                                    capture_output=True)
        self.assertNotEqual(result.returncode, 0)
