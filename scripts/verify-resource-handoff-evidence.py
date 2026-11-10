#!/usr/bin/env python3
"""Bind accepted DXE dispatch and Darwin/USB exit paths to original bytes.

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
DARWIN_GUID = '1B64AF87-8415-4B57-9B16-680D70913B9F'
DARWIN_SHA = '8ce496a6621101c3354dc96cf031fb7ac1dc2ca89775cccddf19a07c41f4051b'
DARWIN_SOURCE = ('gpu-work/out/scm-pair-final-6ce4c7c/firmware/build/'
                 'DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi')
DARWIN_RANGES = ((0x1050, 420), (0x130c, 16), (0x1360, 12),
                 (0x27b8, 340), (0xb000, 148))
USB_GUID = 'CD823A4D-7DEC-4531-AE5D-4134FA4127B8'
USB_SHA = '3dd36eef1cbe67eb65acb5f1825d8ce461d62ab7122d87e7a0a5475ea7636371'
USB_SOURCE = ('uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/'
              'PatchedBinaries/UsbConfigDxe.efi')
USB_RANGES = ((0x1790, 476), (0x16e4, 96), (0x4670, 904),
              (0x7c2c, 1172), (0x935c, 200), (0x9e98, 200),
              (0xf2cc, 2), (0x14028, 16))


def bind_pe(inner, original, guid, digest, source, name):
    require(hashlib.sha256(original).hexdigest() == digest, name + ' identity mismatch')
    require(guid in inner, 'accepted ' + name + ' missing')
    observed = [body for kind, body in ICB.NPA.PAIR.fv.sections(inner[guid][24:])
                if kind == 0x10]
    require(observed == [original], 'embedded ' + name + ' mismatch')
    return {'guid': guid, 'source': source, 'sha256': digest,
            'bytes': len(original), 'embedded_bytes_equal': True}


def bind_core(inner, original):
    return bind_pe(inner, original, CORE_GUID, CORE_SHA, CORE_SOURCE, 'DXE core')


def darwin_exit(blob, segments, reads):
    checks = (
        (0x1064, 'ldr', 'x8, [x1, #0x60]'), (0x107c, 'ldr', 'x8, [x8, #0x50]'),
        (0x1080, 'adrp', 'x2, #0x1000'), (0x1084, 'add', 'x2, x2, #0x30c'),
        (0x108c, 'mov', 'w0, #0x201'), (0x1090, 'mov', 'w1, #0x10'),
        (0x1098, 'blr', 'x8'), (0x130c, 'adrp', 'x8, #0x7000'),
        (0x1310, 'mov', 'w9, #1'), (0x1314, 'strb', 'w9, [x8, #0x8b0]'),
        (0x1318, 'ret', ''), (0x1360, 'mov', 'x28, #0x21'),
        (0x1364, 'movk', 'x28, #0x8000, lsl #48'),
        (0x1368, 'sub', 'x27, x28, #0x1f'),
        (0x27c4, 'mov', 'x21, xzr'), (0x27c8, 'adrp', 'x25, #0x7000'),
        (0x27cc, 'add', 'x25, x25, #0x8d8'),
        (0x27dc, 'cmp', 'x21, #3'), (0x27e0, 'b.eq', '#0x2908'),
        (0x27f0, 'ldr', 'x8, [x8, #0x8e8]'), (0x2804, 'ldr', 'x8, [x8, #0x38]'),
        (0x2808, 'blr', 'x8'), (0x280c, 'tbz', 'x0, #0x3f, #0x2818'),
        (0x2810, 'cbz', 'x21, #0x28f4'), (0x2814, 'bl', '#0x3740'),
        (0x2818, 'ldr', 'x8, [x25, #0x10]'), (0x281c, 'ldur', 'x1, [x29, #-0x70]'),
        (0x2820, 'ldr', 'x0, [x25]'), (0x2824, 'ldr', 'x8, [x8, #0xe8]'),
        (0x2828, 'blr', 'x8'), (0x282c, 'mov', 'x23, x0'),
        (0x2830, 'tbnz', 'x0, #0x3f, #0x28e0'), (0x2834, 'cbz', 'w22, #0x28ac'),
        (0x2838, 'add', 'w11, w21, #1'), (0x2844, 'mov', 'w9, #1'),
        (0x284c, 'stp', 'w11, w9, [x10]'), (0x2860, 'cmp', 'x8, #0xa60'),
        (0x2864, 'b.eq', '#0x288c'), (0x2870, 'b.eq', '#0x2880'),
        (0x2878, 'ldrb', 'w10, [x10, #0x40]'), (0x287c, 'eor', 'x9, x9, x10'),
        (0x2880, 'mul', 'x9, x9, x26'), (0x2888, 'b', '#0x2860'),
        (0x2898, 'str', 'x9, [x1, #0x58]'), (0x28a0, 'mov', 'w2, #0xa60'),
        (0x28a8, 'bl', '#0x3bd8'), (0x28d8, 'bl', '#0xb000'),
        (0x28dc, 'bl', '#0x3740'), (0x28e0, 'cmp', 'x23, x27'),
        (0x28e4, 'b.eq', '#0x28ec'), (0x28e8, 'bl', '#0x3740'),
        (0x28ec, 'add', 'x21, x21, #1'), (0x28f0, 'b', '#0x27dc'),
        (0x2908, 'bl', '#0x3740'),
        (0xb000, 'msr', 'daifset, #0xf'), (0xb01c, 'stp', 'w10, w10, [x11], #8'),
        (0xb02c, 'add', 'x5, x2, x3'), (0xb030, 'mrs', 'x6, ctr_el0'),
        (0xb04c, 'dc', 'civac, x4'), (0xb058, 'b.lo', '#0xb04c'),
        (0xb060, 'ic', 'iallu'), (0xb070, 'and', 'x4, x4, #0xfffffffffffffffe'),
        (0xb074, 'and', 'x4, x4, #0xfffffffffffffffb'),
        (0xb078, 'and', 'x4, x4, #0xffffffffffffefff'),
        (0xb07c, 'msr', 'sctlr_el1, x4'), (0xb084, 'tlbi', 'vmalle1'),
        (0xb08c, 'isb', ''), (0xb090, 'br', 'x1'))
    decoded = CRYPTO.instructions(blob, segments, reads, DARWIN_RANGES, checks)
    return {'boot_entry': '0x133c', 'exit_loop': '0x27b8', 'kernel_transition': '0xb000',
            'event': {'type': '0x201', 'tpl': 16, 'callback': '0x130c',
                      'effect': 'Only store byte 1 at module+0x78b0, then return'},
            'retry': 'At most three EBS attempts; EFI_INVALID_PARAMETER (0x8000000000000002) alone retries with a fresh map',
            'success': 'Optionally seal/copy the 0xa60-byte SCM record, then DarwinEnter',
            'transition': 'Mask DAIF, framebuffer write, DC CIVAC / IC IALLU, disable EL1 MMU/cache, TLBI VMALLE1, BR x1',
            'instructions': decoded,
            'limitations': ['Bounded static exit path, not observed callback registration or execution',
                            'The SCM record is not a resource lease or final-writer snapshot',
                            'CPU cache/TLB maintenance is not GPU DMA/SMMU quiescence or a hardware ACK',
                            'No resource acquire/quiesce backend or unique XNU owner is established']}


def usb_exit(blob, segments, reads):
    require(CRYPTO.read(blob, segments, reads, 0x14028, 16) ==
            uuid.UUID('27abf055-b1b8-4c26-8048-748f37baa2df').bytes_le,
            'USB ExitBootServices GUID mismatch')
    require(CRYPTO.read(blob, segments, reads, 0xf2cc, 2) == b'\x00\x01',
            'USB initial cleanup flags mismatch')
    checks = (
        (0x1850, 'ldr', 'x12, [x10, #0x170]'),
        (0x1844, 'adrp', 'x2, #0x1000'), (0x1854, 'add', 'x2, x2, #0x6e4'),
        (0x1848, 'adrp', 'x4, #0x14000'), (0x1858, 'add', 'x4, x4, #0x28'),
        (0x185c, 'mov', 'w0, #0x200'), (0x1860, 'mov', 'w1, #8'),
        (0x186c, 'blr', 'x12'), (0x16f0, 'bl', '#0x4670'),
        (0x46b8, 'ldr', 'x10, [x10, #0x70]'), (0x46d0, 'blr', 'x10'),
        (0x4710, 'ldr', 'x10, [x10, #0x70]'), (0x4728, 'blr', 'x10'),
        (0x4748, 'cmp', 'x8, #2'), (0x474c, 'b.hs', '#0x48a4'),
        (0x4780, 'ldrb', 'w10, [x9, #0x2cc]'), (0x478c, 'cbnz', 'w10, #0x47b0'),
        (0x4790, 'mov', 'w8, #1'), (0x4798, 'ldr', 'x10, [x9, #0x60]'),
        (0x47a0, 'uxtb', 'w1, w8'), (0x47a4, 'blr', 'x10'),
        (0x4858, 'bl', '#0xa468'), (0x4884, 'bl', '#0xb3c0'),
        (0x48bc, 'cbz', 'w10, #0x49d8'), (0x49ac, 'ldr', 'x10, [x10, #8]'),
        (0x49b4, 'ldr', 'x0, [x11, #0xb38]'), (0x49c4, 'blr', 'x10'),
        (0x49d0, 'str', 'x8, [x9, #0xb38]'),
        (0x7dd0, 'adrp', 'x9, #0x9000'), (0x7dd4, 'add', 'x9, x9, #0x35c'),
        (0x7dd8, 'str', 'x9, [x8, #0x60]'),
        (0x7f48, 'adrp', 'x9, #0x9000'), (0x7f4c, 'add', 'x9, x9, #0xe98'),
        (0x7f50, 'str', 'x9, [x8, #0x60]'),
        (0x93ec, 'cbz', 'w9, #0x93f4'), (0x93f0, 'b', '#0x9404'),
        (0x93fc, 'bl', '#0x7804'), (0x9420, 'ret', ''),
        (0x9f28, 'cbz', 'w9, #0x9f30'), (0x9f2c, 'b', '#0x9f40'),
        (0x9f38, 'bl', '#0x7984'), (0x9f5c, 'ret', ''))
    decoded = CRYPTO.instructions(blob, segments, reads, USB_RANGES, checks)
    return {'registration': {'type': '0x200', 'tpl': 8, 'callback': '0x16e4'},
            'cleanup': 'Close polling events; two core vtable+0x60 calls with arg1, free core/port objects; conditional domain protocol slot+8 call and zero domain pointer',
            'core_vtable': ['0x935c', '0x9e98'],
            'core_arg1': 'Both setters skip their PHY calls for nonzero arg1',
            'instructions': decoded,
            'limitations': ['Initial flags are file bytes, not runtime flags; indirect calls and their results are not observed',
                            'The domain protocol slot+8 ABI/completion is not established and is not GPU/global SMMU teardown',
                            'This bounded cleanup path does not prove ICB votes were cleared or hardware quiesced',
                            'No complete exit writer inventory, ARC snapshot or XNU lease is established']}


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
    extras = {}
    for name, guid, digest, source, evidence_name, analyze in (
            ('darwin', DARWIN_GUID, DARWIN_SHA, DARWIN_SOURCE,
             'darwin-resource-handoff-final', darwin_exit),
            ('usb', USB_GUID, USB_SHA, USB_SOURCE, 'usb-resource-handoff-v2', usb_exit)):
        original = (root / source).read_bytes()
        identity_extra = bind_pe(inner, original, guid, digest, source, name)
        evidence = (root / ('logs/pas13-instruction-replay-20261110/' +
                           evidence_name + '-evidence.json')).read_bytes()
        extra_segments, extra_reads = ICB.checked_reads(original, digest, json.loads(evidence))
        extras[name] = {'identity': identity_extra,
                        'evidence_sha256': hashlib.sha256(evidence).hexdigest(),
                        'read_evidence_ids': list(extra_reads.values()),
                        'exit_path': analyze(original, extra_segments, extra_reads)}
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': accepted['boot_sha256'],
            'dxe_core': identity, 'evidence_sha256': hashlib.sha256(bundle).hexdigest(),
            'read_evidence_ids': list(reads.values()), 'core_order': core_order(blob, segments, reads),
            'exit_consumers': extras,
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
