#!/usr/bin/env python3
"""Small regression tests; numeric ownership checks require root."""
import importlib.util
import os
from pathlib import Path
import plistlib
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "prepare_root", Path(__file__).with_name("prepare-ios-root.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


@unittest.skipUnless(os.geteuid() == 0, "run in root-tools for chown")
class RestoreFiles(unittest.TestCase):
    def test_prepare_is_idempotent_and_uses_upstream_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module.prepare(root)
            bag = root / module.KEYBAG
            first = bag.read_bytes()
            self.assertEqual(plistlib.loads(first), {"KeyBagKeys": b"SWEET"})
            module.prepare(root)
            self.assertEqual(bag.read_bytes(), first)
            module.metadata(bag, 0, 0, 0o600)
            module.metadata(bag.parent, 0, 0, 0o700)
            for name, mode in module.STORES.items():
                d = root / "private/var/db" / name
                module.metadata(d, 272, 272, mode)
                module.metadata(d / ".migrated", 272, 272, 0o644)

    def test_existing_keybag_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module.prepare(root)
            bag = root / module.KEYBAG
            bag.write_bytes(b"existing device state")
            module.prepare(root)
            self.assertEqual(bag.read_bytes(), b"existing device state")

    def test_missing_keybag_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(FileNotFoundError):
                module.verify(Path(tmp))


if __name__ == "__main__":
    unittest.main()
