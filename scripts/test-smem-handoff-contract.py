#!/usr/bin/env python3
"""Portable copied-record checks for kernel CI, without UEFI trees or devices."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='smem-contract-') as directory:
    output = Path(directory) / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    str(ROOT / 'xnu/test_smem_map_handoff.c'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)
