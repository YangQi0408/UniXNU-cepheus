#!/usr/bin/env python3
"""Portable GPU source/staging integration checks, not XNU ABI or runtime tests."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Integration(unittest.TestCase):
    def test_headers_stage_before_patch_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            (tree / 'bsd/conf').mkdir(parents=True)
            (tree / 'bsd/dev').mkdir()
            (tree / 'bsd/conf/files').write_text('base\n')
            subprocess.run(['bash', str(ROOT / 'xnu/0087-stage-cepheus-gpu-memory.sh')], cwd=tree, check=True)
            for where, name in [('bsd/dev', 'cepheus_gpu_compile.c'), ('bsd/dev', 'cepheus_hfi_boot.h'),
                                ('bsd/dev', 'cepheus_smmu_install.h'), ('iokit/Kernel/arm', 'cepheus_gpu_memory.cpp')]:
                self.assertEqual((tree / where / name).read_bytes(), (ROOT / 'xnu' / name).read_bytes())
            self.assertFalse((tree / 'bsd/dev/sweet_rpmh.c').exists())

    def test_patch_places_each_source_once(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            subprocess.run(['git', 'init', '-q', str(tree)], check=True)
            for name, text in {
                'bsd/conf/files': 'bsd/dev/sweet_sample.c\t\t\tstandard\nbsd/dev/sweet_keys.c\t\t\tstandard\n'
                    'bsd/dev/cepheus_touch.c\t\t\tstandard\nbsd/dev/cepheus_soc.c\t\t\tstandard\n'
                    'bsd/dev/sweet_rpmh.c\t\t\tstandard\nbsd/dev/sweet_spmi.c\t\t\tstandard\n',
                'iokit/conf/files.arm64': 'iokit/Kernel/arm/SweetWatchdog.cpp                               optional iokitcpp\n'
                    'iokit/Kernel/arm/SweetSurface.cpp                                optional iokitcpp\n'
                    'iokit/Kernel/arm/SweetFramebuffer.cpp                            optional iokitcpp\n'
                    'iokit/Kernel/arm/SweetBattery.cpp                                optional iokitcpp\n'
                    'iokit/Kernel/arm/hid/SweetHID.cpp                                optional iokitcpp\n',
            }.items():
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            subprocess.run(['git', '-C', str(tree), 'apply', str(ROOT / 'xnu/0087-cepheus-gpu-memory.patch')], check=True)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_gpu_compile.c'), 1)
            self.assertEqual((tree / 'iokit/conf/files.arm64').read_text().count('cepheus_gpu_memory.cpp'), 1)

    def test_wrappers_compile_without_runtime_backend(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-c',
                            str(ROOT / 'xnu/cepheus_gpu_compile.c'), '-o', str(Path(directory) / 'gpu.o')], check=True)


if __name__ == '__main__':
    unittest.main()
