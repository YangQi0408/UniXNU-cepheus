#!/usr/bin/env python3
"""Check actual staged GPU source and linked symbols; no claim of execution."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ('cepheus_hfi.h', 'cepheus_hfi_boot.h', 'cepheus_hfi_messages.h', 'cepheus_hfi_session.h',
           'cepheus_arc.h', 'cepheus_gmu_firmware.h', 'cepheus_gmu_upload.h', 'cepheus_smmu_plan.h',
           'cepheus_smmu_install.h', 'cepheus_gpu_power.h', 'cepheus_gpu_power_txn.h', 'cepheus_gpu_submit.h',
           'cepheus_gpu_compile.c', 'cepheus_gpu_compile.h', 'cepheus_gpu_memory.h')
HEADERS += ('cepheus_gmu_start.h','cepheus_bcm.h')
HEADERS += ('cepheus_gpu_ring.h','cepheus_a640_pm4.h','cepheus_sqe.h')
HEADERS += ('cepheus_gpu_bw.h','cepheus_gpu_firmware.h','cepheus_gpu_fault.h')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xnu', type=Path)
    parser.add_argument('kernel', type=Path)
    args = parser.parse_args()
    for name in HEADERS:
        if (args.xnu / 'bsd/dev' / name).read_bytes() != (ROOT / 'xnu' / name).read_bytes():
            raise ValueError(f'staged BSD source mismatch: {name}')
    for name in ('cepheus_gpu_memory.cpp', 'cepheus_gpu_memory.h', 'cepheus_gpu_memory_state.h'):
        if (args.xnu / 'iokit/Kernel/arm' / name).read_bytes() != (ROOT / 'xnu' / name).read_bytes():
            raise ValueError(f'staged IOKit source mismatch: {name}')
    data = args.kernel.read_bytes()
    if data[:8] != bytes.fromhex('cffaedfe0c000001') or b'CEPHEUS GPU MEM:' not in data:
        raise ValueError('not the expected arm64 GPU kernel')
    if b'SWEET FASTBOOT:' in data or b'sweetfastboot' in data:
        raise ValueError('cancelled USB control code unexpectedly present')
    nm = subprocess.check_output(['xcrun', '--find', 'nm'], text=True).strip() if sys.platform == 'darwin' else shutil.which('llvm-nm')
    if not nm:
        raise ValueError('Mach-O nm is required')
    output = subprocess.check_output([nm, str(args.kernel)], text=True)
    symbols = {line.split()[-1] for line in output.splitlines() if len(line.split()) == 3 and line.split()[-2].upper() != 'U'}
    required = {'_cepheus_gpu_hfi_boot', '_cepheus_gpu_upload', '_cepheus_gpu_smmu_install',
                '_cepheus_gpu_power_start', '_cepheus_gpu_submit', '_cepheus_gpu_memory_selftest', '_cepheus_gpu_gmu_start',
                '_cepheus_gpu_ring_submit', '_cepheus_gpu_sqe_copy','_cepheus_gpu_bandwidth_table','_cepheus_gpu_fault_read'}
    if required - symbols:
        raise ValueError(f'missing linked GPU symbols: {sorted(required - symbols)}')
    memory_api={'_cepheus_gpu_memory_alloc','_cepheus_gpu_memory_view_get','_cepheus_gpu_memory_view_put',
                '_cepheus_gpu_memory_publish','_cepheus_gpu_memory_release_cpu'}
    if memory_api - symbols:
        raise ValueError(f'missing linked memory lifecycle APIs: {sorted(memory_api - symbols)}')
    if subprocess.check_output([nm, '-u', str(args.kernel)], text=True).strip():
        raise ValueError('kernel has unresolved symbols')
    print('GPU staged sources, eleven core entrypoints and five memory lifecycle APIs: PASS (not hardware readiness)')


if __name__ == '__main__':
    main()
