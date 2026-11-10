#!/usr/bin/env python3
"""Bind the accepted DXE core's event ordering to offline original bytes.

No device, firmware execution, protocol invocation, SMC or MMIO access.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import uuid
import zlib

SPEC = importlib.util.spec_from_file_location(
    'crypto_verify', Path(__file__).with_name('verify-crypto-icb-evidence.py'))
CRYPTO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CRYPTO)
ICB = CRYPTO.ICB
require = ICB.require
CORE_GUID = 'D6A2CB7F-6A18-4E2F-B43B-9920A733700A'
CORE_SHA = '311a20a50632dbabdfcdaa9bc2ed7d08e4712c2c70b8f3f18b66834803812be8'
CORE_SOURCE = 'Build/SurfaceDuo1Pkg/RELEASE_CLANGPDB/AARCH64/DxeCore.efi'
CORE_RANGES = ((0x1a80, 436), (0x6194, 128), (0x6120, 116), (0x3dec, 372),
               (0x5f24, 504), (0x133b0, 20), (0x133c4, 24), (0x5f18, 12),
               (0x13394, 28), (0x132ec, 100), (0x15e88, 40),
               (0x18708, 32), (0x18778, 16))


def bind_core(inner, original):
    require(hashlib.sha256(original).hexdigest() == CORE_SHA, 'DXE core identity mismatch')
    require(CORE_GUID in inner, 'accepted DXE core missing')
    observed = [body for kind, body in ICB.NPA.PAIR.fv.sections(inner[CORE_GUID][24:])
                if kind == 0x10]
    require(observed == [original], 'embedded DXE core mismatch')
    return {'guid': CORE_GUID, 'source': CORE_SOURCE, 'sha256': CORE_SHA,
            'bytes': len(original), 'embedded_bytes_equal': True}


def core_order(blob, segments, reads):
    before = CRYPTO.read(blob, segments, reads, 0x18708, 32)
    require(before[:16] == uuid.UUID('8be0e274-3970-4b44-80c5-1ab9502f3bfc').bytes_le,
            'BeforeExitBootServices GUID mismatch')
    require(before[16:] == uuid.UUID('4f6c5507-232f-4787-b95e-72f862490cb1').bytes_le,
            'ExitBootServicesFailed GUID mismatch')
    require(CRYPTO.read(blob, segments, reads, 0x18778, 16) ==
            uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df').bytes_le,
            'ExitBootServices GUID mismatch')
    checks = (
        (0x1a98, 'ldrb', 'w8, [x19, #0x310]'), (0x1a9c, 'tbnz', 'w8, #0, #0x1ab4'),
        (0x1aa4, 'add', 'x0, x0, #0x708'), (0x1aa8, 'bl', '#0x6194'),
        (0x1ab0, 'strb', 'w8, [x19, #0x310]'), (0x1ad0, 'blr', 'x8'),
        (0x1ad8, 'ldr', 'x8, [x21]'), (0x1adc, 'cmp', 'x8, x20'),
        (0x1ae0, 'b.ne', '#0x1b5c'), (0x1b6c, 'cbz', 'w20, #0x1b80'),
        (0x1b74, 'add', 'x0, x0, #0x718'), (0x1b78, 'bl', '#0x6194'),
        (0x1b90, 'add', 'x0, x0, #0x778'), (0x1b94, 'strb', 'w20, [x21]'),
        (0x1b98, 'bl', '#0x6194'), (0x1bec, 'blr', 'x8'),
        (0x1bf4, 'str', 'xzr, [x0, #0x60]'), (0x1c10, 'bl', '#0x3414'),
        (0x1c18, 'str', 'xzr, [x22, #0x48]'),
        (0x60e4, 'add', 'x0, x0, #0x758'), (0x60e8, 'add', 'x1, x26, #0x10'),
        (0x60ec, 'bl', '#0x133b0'),
        (0x133b0, 'ldr', 'x8, [x0]'), (0x133b4, 'stp', 'x8, x0, [x1]'),
        (0x133b8, 'str', 'x1, [x8, #8]'), (0x133bc, 'str', 'x1, [x0]'),
        (0x61c4, 'ldr', 'x23, [x23]'), (0x61e4, 'add', 'x0, x20, #0x38'),
        (0x61e8, 'bl', '#0x2fcc'), (0x61f8, 'bl', '#0x6120'), (0x6210, 'b', '#0x5f18'),
        (0x6138, 'ldr', 'x8, [x20, #0x48]!'), (0x6144, 'bl', '#0x13420'),
        (0x6164, 'bl', '#0x133c4'),
        (0x133c4, 'str', 'x0, [x1]'), (0x133c8, 'ldr', 'x8, [x0, #8]'),
        (0x133cc, 'str', 'x8, [x1, #8]'), (0x133d0, 'str', 'x1, [x8]'),
        (0x133d4, 'str', 'x1, [x0, #8]'),
        (0x5f20, 'b', '#0x13394'), (0x13398, 'ldr', 'x0, [x0, #8]'),
        (0x133a4, 'b', '#0x3dec'),
        (0x3e54, 'bl', '#0x132ec'), (0x3e58, 'cmp', 'x0, x19'),
        (0x3e5c, 'b.ls', '#0x3f18'), (0x132f4, 'lsr', 'x8, x0, #0x20'),
        (0x13308, 'bl', '#0x15e88'), (0x15e90, 'mov', 'w0, #0x1f'),
        (0x15e94, 'cmp', 'w8, #1'), (0x15e98, 'b.lt', '#0x15eac'),
        (0x15e9c, 'lsl', 'w8, w8, #1'), (0x15ea0, 'sub', 'x0, x0, #1'),
        (0x15ea4, 'b', '#0x15e94'), (0x15eac, 'ret', ''),
        (0x3e94, 'ldr', 'x8, [x21]'), (0x3ea8, 'bl', '#0x13420'),
        (0x3ebc, 'bl', '#0x5f18'), (0x3ee4, 'ldp', 'x8, x1, [x22, #0x28]'),
        (0x3eec, 'blr', 'x8'), (0x3ef4, 'b', '#0x3e84'),
        (0x3f14, 'b', '#0x3e48'))
    decoded = CRYPTO.instructions(blob, segments, reads, CORE_RANGES, checks)
    return {'exit_entry': '0x1a80', 'group_dispatch': '0x6194', 'restore_tpl': '0x3dec',
            'sequence': ['BeforeExitBootServices once', 'timer disable',
                         'map-key and runtime-map validation',
                         'failure group and return OR mark terminated then EBS group',
                         'memory protection and CPU interrupt disable',
                         'clear system-table BootServices and zero boot-services table'],
            'registration': 'InsertHeadList into group signal list',
            'dispatch': 'Forward traversal -> InsertTailList into each NotifyTpl queue',
            'order': 'Highest pending TPL above caller TPL first; FIFO at each TPL, usually reverse registration order',
            'callbacks': 'Direct synchronous BLR; release event lock before callback; drain until queue empty',
            'retry': 'Before group runs once even on map-key failure; failure group runs; EBS group only on successful validation',
            'instructions': decoded,
            'limitations': ['Actual runtime event inventory, caller TPL and successful registrations are not captured',
                            'Prequeued/requeued events, nesting and callback mutations can alter simple registration order',
                            'Callback return is not resource ACK; hardware requests or foreign writers can outlive it',
                            'No last-writer snapshot, ARC ownership or XNU lease is established']}


def verify(root):
    accepted = ICB.NPA.verify(root)
    boot = (root / 'gpu-work/out/scm-pair-final-6ce4c7c/boot-scm-pair.img').read_bytes()
    require(hashlib.sha256(boot).hexdigest() == accepted['boot_sha256'], 'boot changed during verification')
    size = struct.unpack_from('<I', boot, 8)[0]
    raw = zlib.decompressobj(31).decompress(boot[4096:4096 + size], 0x300081)
    require(len(raw) == 0x300080, 'firmware payload size mismatch')
    _, inner = ICB.NPA.PAIR.inner_files(raw[128:])
    blob = (root / 'uefi' / CORE_SOURCE).read_bytes()
    identity = bind_core(inner, blob)
    bundle = (root / 'logs/pas13-instruction-replay-20261110/dxecore-resource-handoff-final-evidence.json').read_bytes()
    segments, reads = ICB.checked_reads(blob, CORE_SHA, json.loads(bundle))
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': accepted['boot_sha256'],
            'dxe_core': identity, 'evidence_sha256': hashlib.sha256(bundle).hexdigest(),
            'read_evidence_ids': list(reads.values()), 'core_order': core_order(blob, segments, reads),
            'phone_operations': False, 'gpu_execution': False, 'pas_authentication': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.root.resolve())
    with args.output.open('x') as output:
        json.dump(result, output, indent=2)
        output.write('\n')
    print(result['status'])


if __name__ == '__main__':
    main()
