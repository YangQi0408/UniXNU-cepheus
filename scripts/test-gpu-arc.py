#!/usr/bin/env python3
"""Apply-check ARC include after the real touch-power transformation, privately."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("touch_power", ROOT / "scripts/prepare-touch-power-patch.py")
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)
PATCHES = Path(sys.argv[1]) if len(sys.argv) > 1 else None


class ArcIntegrationTests(unittest.TestCase):
    def test_stage_before_source_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            (tree / "bsd/conf").mkdir(parents=True)
            (tree / "bsd/dev").mkdir()
            (tree / "bsd/conf/files").write_text("base XNU inventory\n")
            subprocess.run(["bash", str(ROOT / "xnu/0086-stage-cepheus-gpu-arc.sh")],
                           cwd=tree, check=True)
            for name in ("cepheus_arc.h", "cepheus_arc_probe.h"):
                self.assertEqual((tree / "bsd/dev" / name).read_bytes(), (ROOT / "xnu" / name).read_bytes())
            self.assertFalse((tree / "bsd/dev/sweet_rpmh.c").exists())

    def test_real_patch_after_touch_enable_only(self):
        self.assertIsNotNone(PATCHES, "supply pinned ios-sweet/xnu/patches")
        self.assertTrue(PATCHES.is_dir())
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            subprocess.run(["git", "init", "-q", str(tree)], check=True)
            target = "bsd/dev/sweet_rpmh.c"
            for patch in sorted(PATCHES.glob("*.patch")):
                if patch.name == "0086-cepheus-gpu-arc.patch":
                    continue
                subprocess.run(["git", "-C", str(tree), "apply", "--whitespace=nowarn",
                                f"--include={target}", str(patch.resolve())], check=True)
            file = tree / target
            before = power.transform(file.read_text())
            file.write_text(before)
            subprocess.run(["git", "-C", str(tree), "apply",
                            str(ROOT / "xnu/0086-cepheus-gpu-arc.patch")], check=True)
            after = file.read_text()
            self.assertEqual(after.count('#include "cepheus_arc_probe.h"'), 1)
            self.assertEqual(after.replace('#include "cepheus_arc_probe.h"\n', ''), before)
            self.assertTrue(after.rstrip().endswith('#include "cepheus_arc_probe.h"\n#endif /* ARM64_BOARD_CONFIG_SWEET */'))

    def test_probe_has_no_transport_write_or_send_calls(self):
        probe = (ROOT / "xnu/cepheus_arc_probe.h").read_text()
        for name in ("rh_write", "rh_sync", "rh_send"):
            self.assertNotRegex(probe, rf"\b{name}\s*\(")
        self.assertIn("__atomic_exchange_n(&rh_lock", probe)
        self.assertIn("__atomic_store_n(&rh_lock, 0", probe)
        self.assertIn("current ARC vote UNKNOWN", probe)

    def test_generated_0084_does_not_depend_on_later_arc_include(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            patches = base / "patches"
            patches.mkdir()
            for patch in PATCHES.glob("*.patch"):
                if patch.name != "0086-cepheus-gpu-arc.patch":
                    shutil.copyfile(patch, patches / patch.name)
            shutil.copyfile(ROOT / "xnu/0086-cepheus-gpu-arc.patch", patches / "0086-cepheus-gpu-arc.patch")
            generated = base / "0084.patch"
            subprocess.run([sys.executable, str(ROOT / "scripts/prepare-touch-power-patch.py"),
                            str(patches), str(generated)], check=True)
            self.assertNotIn("cepheus_arc_probe.h", generated.read_text())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
