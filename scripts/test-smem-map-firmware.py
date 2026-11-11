#!/usr/bin/env python3
"""Reject damaged native diagnostic artifacts and foreign staging profiles."""
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


VERIFY = module('smem_verify_test', 'verify-smem-map-firmware.py')
STAGE = VERIFY.STAGE


def reject(blob):
    try:
        VERIFY.linked(blob)
    except (ValueError, struct.error):
        return
    raise ValueError('damaged linked image accepted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--uefi', required=True, type=Path)
    args = parser.parse_args()
    artifact = args.build / 'build/SmemMapProbe/RELEASE_CLANGPDB/AARCH64/CepheusSmemMapProbe.efi'
    original = artifact.read_bytes()
    report = VERIFY.linked(original)
    pe = struct.unpack_from('<I', original, 60)[0]
    segments = VERIFY.PE.pe_segments(original)
    address = int(report['native_at_rva'], 16)
    code_offset = next(offset + address - base for offset, base, size in segments
                       if base <= address < base + size)
    tests = 0
    for offset, value in ((0, b'XX'), (pe + 4, b'\x64\x86'),
                          (pe + 24 + 68, b'\x0a\0'),
                          (pe + 24 + 120, struct.pack('<I', 4096)),
                          (pe + 24 + 124, struct.pack('<I', 8)),
                          (pe + 24 + 16, struct.pack('<I', 0)),
                          (code_offset, bytes.fromhex('1f2003d5')),
                          (code_offset + 12, bytes.fromhex('1f2003d5')),
                          (code_offset + 16, bytes.fromhex('1f2003d5')),
                          (code_offset, bytes.fromhex('030000d4'))):
        blob = bytearray(original); blob[offset:offset + len(value)] = value
        reject(bytes(blob)); tests += 1
    for size in (0, 63, pe + 23, len(original) - 512):
        reject(original[:size]); tests += 1
    marker = 'cephsmemmap=1\0'.encode('utf-16le')
    reject(original.replace(marker, 'cephsmemmap=0\0'.encode('utf-16le'))); tests += 1
    with tempfile.TemporaryDirectory(prefix='smem-stage-reject-') as directory:
        work = Path(directory)
        STAGE.stage(args.uefi.resolve(), work / 'valid')
        try:
            STAGE.stage(args.uefi.resolve(), work / 'valid')
        except ValueError:
            tests += 1
        else:
            raise ValueError('existing stage overwritten')
        foreign = work / 'foreign'
        for path in (STAGE.PROFILE, STAGE.HEADER):
            target = foreign / path; target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((args.uefi / path).read_bytes())
        for path in (STAGE.PROFILE, STAGE.HEADER):
            target = foreign / path; saved = target.read_bytes(); target.write_bytes(saved + b'\n')
            output = work / ('bad-' + target.name)
            try:
                STAGE.stage(foreign, output)
            except ValueError:
                if output.exists():
                    raise ValueError('bad profile created stage before rejection')
                tests += 1
            else:
                raise ValueError('foreign platform source accepted')
            target.write_bytes(saved)
    print(f'SMEM linked/staging verifier: {tests} rejection cases PASS; original EFI is static evidence only')


if __name__ == '__main__':
    main()
