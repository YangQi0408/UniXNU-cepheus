import importlib.util
import os
import re
import subprocess
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

stage_path = Path(__file__).parents[1] / "scripts" / "stage-private-fastboot-key.py"
stage_spec = importlib.util.spec_from_file_location("stage_fastboot_key", stage_path)
stage_fastboot_key = importlib.util.module_from_spec(stage_spec)
sys.modules[stage_spec.name] = stage_fastboot_key
stage_spec.loader.exec_module(stage_fastboot_key)


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

    def test_fifo_and_hardlinked_keys_are_rejected_without_blocking(self):
        root = self._root()
        try:
            parent = root / control_key.KEY.parent
            parent.mkdir(parents=True)
            key = root / control_key.KEY
            os.mkfifo(key)
            with self.assertRaises(ValueError):
                control_key.verify(root)
            key.unlink()
            key.write_bytes(b"x" * 32)
            key.chmod(0o600)
            os.link(key, parent / "alias")
            with self.assertRaises(ValueError):
                control_key.verify(root)
        finally:
            for child in sorted(root.rglob("*"), reverse=True):
                if child.is_symlink() or child.is_file():
                    child.unlink()
                elif child.is_dir():
                    child.rmdir()
            root.rmdir()

    def test_private_staging_creates_private_header_and_refuses_replace(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / "secret.input"
            source.write_bytes(bytes(range(32)))
            source.chmod(0o600)
            xnu = base / "private-xnu"
            dev = xnu / "bsd/dev"
            dev.mkdir(parents=True)
            (xnu / "bsd/conf").mkdir()
            (xnu / "bsd/conf/files").write_text("inventory")
            (dev / "sweet_usb.c").write_text("sweet_usb_gadget_start\nud_if_create")
            stage_fastboot_key.stage(source, xnu)
            endpoint = (Path(__file__).parents[1] / "xnu/cepheus_fastboot.c").read_text()
            included = re.search(r'#include "([^" ]*fastboot_private\.h)"', endpoint).group(1)
            header = dev / included
            self.assertEqual(stat.S_IMODE(header.stat().st_mode), 0o600)
            self.assertIn("CEPHEUS_FASTBOOT_PROVISIONED 1", header.read_text())
            probe = dev / "header_probe.c"
            probe.write_text(f'#include "{included}"\n#if CEPHEUS_FASTBOOT_PROVISIONED != 1\n#error not provisioned\n#endif\nint main(void) {{ return cepheus_fastboot_key[31] == 31 ? 0 : 1; }}\n')
            binary = base / "header-probe"
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(probe), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            with self.assertRaises(ValueError):
                stage_fastboot_key.stage(source, xnu)

    def test_private_staging_rejects_hardlink_and_git_root(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / "secret.input"
            source.write_bytes(b"k" * 32)
            source.chmod(0o600)
            alias = base / "alias.input"
            os.link(source, alias)
            with self.assertRaises(ValueError):
                stage_fastboot_key.read_private_key(alias)
            repo = base / "repo"
            xnu = repo / "private-xnu"
            (xnu / "bsd/dev").mkdir(parents=True)
            (xnu / "bsd/dev/sweet_usb.c").write_text("sweet_usb_gadget_start\nud_if_create")
            (xnu / "bsd/conf").mkdir()
            (xnu / "bsd/conf/files").write_text("inventory")
            (repo / ".git").mkdir()
            with self.assertRaises(ValueError):
                stage_fastboot_key.stage(source, xnu)

    def test_fastboot_bootarg_default_is_off_and_optin_is_explicit(self):
        root = Path(__file__).parents[1]
        board = (root / "board.toml").read_text()
        patch = (root / "xnu/0087-cepheus-fastboot.patch").read_text()
        self.assertIn("sweetfastboot=0", board)
        self.assertIn("int fastboot = 0", patch)
        self.assertIn("fastboot == 1", patch)
        self.assertIn("cepheus_fastboot_start(ud.ifp)", patch)


if __name__ == "__main__":
    unittest.main()
