import importlib.util
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest

path = Path(__file__).parents[1] / "scripts" / "prepare-control-key.py"
spec = importlib.util.spec_from_file_location("control_key", path)
control_key = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = control_key
spec.loader.exec_module(control_key)


class KeyProvisioningTests(unittest.TestCase):
    def _root(self):
        root = Path(tempfile.mkdtemp())
        marker = root / "System/Library/xpc/launchd.plist"
        marker.parent.mkdir(parents=True)
        marker.write_bytes(b"marker")
        return root

    def test_symlinked_key_parent_is_refused(self):
        root = self._root()
        outside = Path(tempfile.mkdtemp())
        try:
            target = root / "private/var/db"
            target.mkdir(parents=True)
            (target / "sweet-fastboot").symlink_to(outside, target_is_directory=True)
            with self.assertRaises(OSError):
                control_key._open_key_parent(root, create=True)
            self.assertFalse((outside / "control.key").exists())
        finally:
            for child in sorted(root.rglob("*"), reverse=True):
                if child.is_symlink() or child.is_file():
                    child.unlink()
                elif child.is_dir():
                    child.rmdir()
            root.rmdir()
            outside.rmdir()

    def test_verify_rejects_wrong_metadata_without_reading_secret(self):
        root = self._root()
        try:
            parent = root / control_key.KEY.parent
            parent.mkdir(parents=True)
            key = root / control_key.KEY
            key.write_bytes(b"x" * 32)
            key.chmod(0o644)
            with self.assertRaises(ValueError):
                control_key.verify(root)
            self.assertEqual(stat.S_IMODE(key.stat().st_mode), 0o644)
        finally:
            for child in sorted(root.rglob("*"), reverse=True):
                if child.is_file():
                    child.unlink()
                elif child.is_dir():
                    child.rmdir()
            root.rmdir()


if __name__ == "__main__":
    unittest.main()
