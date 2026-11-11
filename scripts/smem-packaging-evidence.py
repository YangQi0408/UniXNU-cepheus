#!/usr/bin/env python3
"""Bind a local SMEM diagnostic package to independently verified XNU bytes."""
import hashlib
import importlib.util
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('smem_packaging_kernel', ROOT / 'scripts/verify-smem-map-kernel.py')
NATIVE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(NATIVE)


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def validate(kernel, receipt):
    require(__debug__, 'SMEM packaging uses legacy parsers with assertions; optimization is refused')
    require(receipt is not None, 'verified full-kernel receipt required')
    report = json.loads(Path(receipt).read_text())
    data = Path(kernel).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    require(report.get('status') == 'PASS' and report.get('kernel_sha256') == digest and
            report.get('kernel_bytes') == len(data), 'kernel receipt does not match passed artifact')
    require(report.get('smem_map_abi_source_verified') is True and
            report.get('native_smem_receiver_and_thread_verified') is True and
            report.get('ci_staged_source_comparisons_passed') is True and
            report.get('unresolved_symbols') == [], 'missing full-kernel link/source evidence')
    state = report.get('run_state', {})
    require(state.get('headSha') == report.get('commit') and state.get('status') == 'completed' and
            state.get('conclusion') == 'success', 'missing exact successful CI identity')
    sources = report.get('source_sha256', {})
    for source in sorted((ROOT / 'xnu').iterdir()):
        if source.is_file() and not source.name.startswith('test_'):
            require(sources.get(source.name) == hashlib.sha256(source.read_bytes()).hexdigest(),
                    'kernel source receipt differs: ' + source.name)
    native = NATIVE.verify(Path(kernel))
    require(report.get('smem_map_verification') == native, 'receiver verification differs from receipt')
    require(report.get('phone_operations') is False and report.get('gpu_execution') is False,
            'unexpected receipt scope')
    return native
