#!/usr/bin/env python3
"""Reject damaged native handoff EFI and mismatched pinned platform staging."""
import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


VERIFY = module('smem_handoff_verify_tests', 'verify-smem-handoff-firmware.py')


def reject(blob):
    try:
        VERIFY.linked(blob)
    except (ValueError, struct.error):
        return
    raise ValueError('accepted damaged EFI')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'repository', 'uefi'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    artifact = args.build / 'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
    blob = artifact.read_bytes()
    report = VERIFY.linked(blob)
    pe = struct.unpack_from('<I', blob, 60)[0]
    address = int(report['native_at_rva'], 16)
    segments = VERIFY.PE.pe_segments(blob)
    code_offset = next(offset + address - base for offset, base, size in segments if base <= address < base + size)
    count = 0
    for offset, value in ((0, b'XX'), (pe + 4, b'\x64\x86'), (pe + 24 + 68, b'\x0a\0'),
                          (pe + 24 + 16, bytes(4)), (pe + 24 + 120, struct.pack('<II', 4096, 8)),
                          (code_offset, bytes.fromhex('1f2003d5')), (code_offset + 12, bytes.fromhex('1f2003d5')),
                          (code_offset + 16, bytes.fromhex('1f2003d5')), (code_offset, bytes.fromhex('030000d4'))):
        damaged = bytearray(blob)
        damaged[offset:offset + len(value)] = value
        reject(bytes(damaged))
        count += 1
    for marker in (b'cepheus-smem-map\0', b'cephsmemmap\0'):
        reject(blob.replace(marker, bytes(len(marker))))
        count += 1
    for size in (0, 63, pe + 23, len(blob) - 512):
        reject(blob[:size])
        count += 1
    with tempfile.TemporaryDirectory(prefix='smem-handoff-reject-') as directory:
        work = Path(directory)
        VERIFY.STAGE.stage(args.repository.resolve(), args.uefi.resolve(), work / 'good')
        try:
            VERIFY.STAGE.stage(args.repository.resolve(), args.uefi.resolve(), work / 'good')
        except ValueError:
            count += 1
        else:
            raise ValueError('existing staging overwritten')
        foreign = work / 'foreign'
        for path in (VERIFY.STAGE.MAP.PROFILE, VERIFY.STAGE.MAP.HEADER):
            target = foreign / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((args.uefi / path).read_bytes())
        for index, path in enumerate((VERIFY.STAGE.MAP.PROFILE, VERIFY.STAGE.MAP.HEADER)):
            target = foreign / path
            saved = target.read_bytes()
            target.write_bytes(saved + b'\n')
            output = work / ('bad-' + str(index))
            try:
                VERIFY.STAGE.stage(args.repository.resolve(), foreign, output)
            except ValueError:
                if output.exists():
                    raise ValueError('foreign platform created output')
                count += 1
            else:
                raise ValueError('foreign platform accepted')
            target.write_bytes(saved)
    print(f'SMEM native handoff/staging: {count} rejection cases PASS; no execution')


if __name__ == '__main__':
    main()
