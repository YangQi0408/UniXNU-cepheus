#!/usr/bin/env python3
"""Bind accepted exits and per-client request completion to original bytes.

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
DISPLAY_GUID = '4138022F-06C7-4F79-9C94-7E33B511A4E7'
DISPLAY_SHA = '7a1799156845c0934164feb87831157f56dd8d5fa5a16356314bede5a64d3552'
DISPLAY_SOURCE = ('uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/'
                  'PatchedBinaries/DisplayDxe.efi')
DISPLAY_RANGES = ((0x1708, 92), (0x14dc, 208), (0x13c28, 532),
                  (0x13b58, 208), (0x1261c, 584), (0x124fc, 156), (0x1eb08, 84))
NPA_GUID = 'CB29F4D1-7F37-4692-A416-93E82E219711'
NPA_SHA = 'a08453756c58db8d187eaa515c8bfb938226b3b8c3f4273e68d9cfc6afc49640'
NPA_SOURCE = ('uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/'
              'Binaries/QcomPkg/Drivers/NpaDxe/NpaDxe.efi')
NPA_RANGES = ((0x110b8, 32), (0x145c, 24), (0x391c, 128), (0x91a8, 120),
              (0x3cd8, 144), (0x8f64, 24), (0x29b0, 100), (0x9480, 16),
              (0x8f7c, 408), (0x922c, 44), (0x9258, 264), (0x3a7c, 604))
CLOCK_GUID = '4DB5DEA6-5302-4D1A-8A82-677A683B0D29'
CLOCK_SHA = 'babe80a2b671823b1c0296dc0adb26e3642aa70d6ed58562d1c6cfea88e81dfe'
CLOCK_SOURCE = ('uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/'
                'Binaries/QcomPkg/Drivers/ClockDxe/ClockDxe.efi')
CLOCK_COMPLETE_RANGES = ((0x432a0, 56), (0xc704, 556), (0xd720, 292),
                         (0xd270, 36), (0xc930, 220))


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


def display_exit(blob, segments, reads):
    checks = (
        (0x170c, 'adrp', 'x2, #0x1000'), (0x1718, 'add', 'x2, x2, #0x4dc'),
        (0x1720, 'mov', 'w0, #0x201'), (0x1724, 'mov', 'w1, #8'),
        (0x1728, 'ldr', 'x8, [x12, #0x50]'), (0x1730, 'blr', 'x8'),
        (0x14fc, 'ldr', 'x8, [x8, #0x70]'), (0x1500, 'blr', 'x8'),
        (0x1520, 'ldr', 'x9, [x9, #0x70]'), (0x1524, 'blr', 'x9'),
        (0x1594, 'mov', 'w0, wzr'), (0x1598, 'bl', '#0x2d64'),
        (0x15a8, 'b', '#0x13c28'),
        (0x13c60, 'mov', 'w25, #1'), (0x13c78, 'cbz', 'x8, #0x13d48'),
        (0x13c7c, 'ldrb', 'w12, [x24, #0x5a3]'), (0x13c80, 'tst', 'w12, #1'),
        (0x13c84, 'csel', 'x25, x25, xzr, eq'),
        (0x13c88, 'cbnz', 'x25, #0x13d18'), (0x13c8c, 'ldr', 'x8, [x24, #0x18]'),
        (0x13c90, 'cmp', 'x8, #1'), (0x13c94, 'b.ne', '#0x13d18'),
        (0x13cac, 'bl', '#0x13a68'), (0x13cd0, 'mov', 'w2, wzr'),
        (0x13cd4, 'bl', '#0x12e14'), (0x13cf4, 'mov', 'w2, #1'),
        (0x13cfc, 'bl', '#0x12e14'),
        (0x13d4c, 'add', 'x24, x24, #0x628'), (0x13d50, 'cmp', 'x19, #3'),
        (0x13d54, 'b.ne', '#0x13c74'), (0x13d58, 'cbnz', 'x25, #0x13d7c'),
        (0x13d5c, 'bl', '#0x13b58'), (0x13e1c, 'bl', '#0x1a3dc'),
        (0x13e30, 'mov', 'w0, wzr'), (0x13e38, 'ret', ''),
        (0x13b64, 'mov', 'w0, wzr'), (0x13b68, 'bl', '#0x1261c'),
        (0x13b88, 'mov', 'w0, #1'), (0x13b8c, 'bl', '#0x1261c'),
        (0x13bac, 'mov', 'w0, #2'), (0x13bb0, 'bl', '#0x1261c'),
        (0x13bd0, 'mov', 'w0, #3'), (0x13bd4, 'bl', '#0x1261c'),
        (0x13bf4, 'mov', 'w0, #4'), (0x13bf8, 'bl', '#0x1261c'),
        (0x12650, 'ldr', 'x8, [x11, #0x140]'), (0x12654, 'blr', 'x8'),
        (0x12720, 'tbnz', 'w9, #0, #0x1276c'),
        (0x12730, 'ldr', 'x14, [x0, #8]'), (0x12734, 'blr', 'x14'),
        (0x12764, 'ldr', 'x16, [x0, #0x18]'), (0x12768, 'blr', 'x16'),
        (0x12840, 'mov', 'w0, wzr'), (0x12844, 'bl', '#0x124fc'),
        (0x12530, 'mov', 'w2, #0x400'), (0x12534, 'mov', 'w3, #0x10'),
        (0x12538, 'bl', '#0x1e95c'), (0x1254c, 'cmp', 'w9, #1'),
        (0x12550, 'b.ne', '#0x12570'), (0x1256c, 'b', '#0x1ea84'),
        (0x1257c, 'b', '#0x1eb08'), (0x1eb30, 'ldr', 'x9, [x9, #0x140]'),
        (0x1eb34, 'blr', 'x9'), (0x1eb60, 'ldr', 'x10, [x10, #0x18]'),
        (0x1eb64, 'blr', 'x10'))
    decoded = CRYPTO.instructions(blob, segments, reads, DISPLAY_RANGES, checks)
    return {'registration': {'type': '0x201', 'tpl': 8, 'callback': '0x14dc'},
            'cleanup': 'Close UI/refresh events, unregister low-power callback, tail-call 0x13c28',
            'condition': 'Three-display cumulative keep latch starts 1 and becomes 0 when a supported entry has bit0 at state+0x5a3; shutdown also requires state+0x18 == 1',
            'shutdown': 'Conditional panel deinit/common+specific power calls; when cumulative latch is 0, call clock groups 0..4; free objects and final display-state copy',
            'clock_path': '0x1261c selects dynamic clock tables; skips flagged entries, invokes clock protocol slots+8/+0x18 and conditional core bandwidth helper',
            'bandwidth_path': '0x124fc creates a display ICB client if absent, then request helper for arg1 or NPA slot+0x18 helper otherwise',
            'instructions': decoded,
            'limitations': ['No runtime supported-display inventory, cumulative latch, clock tables or successful registrations are captured',
                            'Old-style exit event exists despite no literal EBS group GUID in this PE; GUID absence alone is not callback absence',
                            'Clock/ICB calls and ignored failures are not live ACKs or global quiescence; bound NPA slot+0x18 completes only this client request',
                            'Final display-state copy is not a complete ARC/client snapshot or unique XNU resource lease']}


def npa_complete(blob, segments, reads):
    table = CRYPTO.read(blob, segments, reads, 0x110b8, 32)
    require(struct.unpack('<4Q', table) == (0x10002, 0x1418, 0x1430, 0x145c),
            'NPA complete protocol table mismatch')
    checks = (
        (0x1464, 'bl', '#0x391c'), (0x1468, 'mov', 'x0, xzr'),
        (0x3928, 'mov', 'x19, x0'), (0x395c, 'mov', 'x0, x19'),
        (0x3964, 'b', '#0x91a8'), (0x91b4, 'mov', 'w1, #1'),
        (0x91bc, 'bl', '#0x3cd8'), (0x3cec, 'ldr', 'w8, [x0, #0x80]'),
        (0x3cf0, 'orr', 'w9, w8, w1'), (0x3cf4, 'str', 'w9, [x0, #0x80]'),
        (0x91c0, 'ldr', 'w8, [x19, #0x30]'), (0x91c4, 'cmp', 'w8, #0x200'),
        (0x91c8, 'b.ne', '#0x91e0'), (0x91d4, 'mov', 'w1, wzr'),
        (0x91dc, 'b', '#0x31c8'), (0x91e0, 'ldr', 'w8, [x19, #0x68]'),
        (0x91e4, 'add', 'x10, x19, #0x38'), (0x91ec, 'eor', 'w9, w8, #1'),
        (0x91f0, 'mov', 'w8, #0x18'), (0x91f4, 'umull', 'x9, w9, w8'),
        (0x91f8, 'str', 'wzr, [x10, x9]'), (0x9200, 'ldr', 'x2, [x19, #0x88]'),
        (0x9208, 'mul', 'x12, x11, x8'), (0x920c, 'ldr', 'w10, [x10, x12]'),
        (0x9214, 'cset', 'w1, ne'), (0x921c, 'br', 'x2'),
        (0x8f64, 'cbz', 'w1, #0x8f6c'), (0x8f68, 'b', '#0x8f7c'),
        (0x8f70, 'orr', 'w8, w8, #1'), (0x8f78, 'b', '#0x8e70'),
        (0x29c8, 'cbz', 'w8, #0x29f0'), (0x29d4, 'bl', '#0x9480'),
        (0x29d8, 'cbz', 'w0, #0x29f0'), (0x9484, 'ldr', 'w8, [x0, #0x80]'),
        (0x9488, 'and', 'w0, w8, w1'), (0x29f0, 'mov', 'w1, #1'),
        (0x29f8, 'bl', '#0x8f64'), (0x2a00, 'add', 'x8, x8, #0xf64'),
        (0x2a04, 'str', 'x8, [x19, #0x88]'),
        (0x8f94, 'ldr', 'x20, [x19, #0x20]'), (0x9048, 'bl', '#0x922c'),
        (0x9054, 'bl', '#0x9258'), (0x923c, 'ldr', 'x8, [x19, #0x28]'),
        (0x9240, 'ldr', 'x8, [x8]'), (0x9244, 'blr', 'x8'),
        (0x9248, 'str', 'w0, [x19, #0x30]'),
        (0x9298, 'tbnz', 'w8, #0, #0x92f0'),
        (0x92a4, 'b.ne', '#0x92f0'), (0x92ec, 'tbz', 'w1, #2, #0x9348'),
        (0x92f4, 'ldr', 'x13, [x19, #0x10]'),
        (0x9308, 'ldr', 'x8, [x13, #8]'), (0x930c, 'blr', 'x8'),
        (0x935c, 'b', '#0x8ce8'), (0x9068, 'eor', 'w10, w10, #1'),
        (0x906c, 'stp', 'w10, w9, [x19, #0x68]'),
        (0x3ad8, 'add', 'x9, x9, #0xf64'),
        (0x3ae0, 'str', 'x9, [x19, #0x88]'), (0x3ae4, 'bl', '#0x391c'),
        (0x3c58, 'ldr', 'x12, [x25, #0x28]'),
        (0x3c5c, 'ldr', 'x8, [x12, #0x18]'), (0x3c68, 'blr', 'x8'))
    decoded = CRYPTO.instructions(blob, segments, reads, NPA_RANGES, checks)
    return {'protocol': {'revision': '0x10002', 'slot': '0x18', 'wrapper': '0x145c'},
            'client_register': 'x0, not the misleading extra pseudocode parameter',
            'pending': 'Set complete attribute bit0 at client+0x80; non-limit-max path zeros only the pending first word at client+0x38+((active_index XOR 1)*0x18)',
            'dispatch': 'client+0x88 receives whether the old active first word was nonzero; 0x29b0 can force the first update, then install 0x8f64',
            'update': '0x8f7c calls resource plugin then conditional node driver, toggles active index and updates software sequence',
            'destroy': 'Separate 0x3a7c completes first, unlinks the client and invokes plugin destroy slot+0x18; this is not protocol complete slot+0x18',
            'instructions': decoded,
            'limitations': ['Static per-client completion, not a live ACK or clearing all clients',
                            'Type0x200 delegates to 0x31c8; its limit-max body is outside this claim',
                            'Transaction, lock, callback and no-change driver branches are runtime-dependent',
                            'No complete resource snapshot, final ARC state, PAS support or XNU owner is established']}


def clock_complete(blob, segments, reads):
    plugin = struct.unpack('<7Q', CRYPTO.read(blob, segments, reads, 0x432a0, 56))
    require(plugin == (0xc704, 0x1400, 0, 0xc930, 0xca0c, 0, 0xcd50),
            'Clock ICB plugin table mismatch')
    checks = (
        (0xc724, 'mov', 'x19, x1'), (0xc738, 'ldr', 'x21, [x19, #0x28]'),
        (0xc730, 'ldr', 'w9, [x19, #0x68]'), (0xc73c, 'eor', 'w11, w9, #1'),
        (0xc72c, 'mov', 'w8, #0x18'), (0xc740, 'umaddl', 'x9, w11, w8, x19'),
        (0xc744, 'ldr', 'w11, [x9, #0x38]'), (0xc754, 'lsr', 'w24, w11, #3'),
        (0xc758, 'cbz', 'w24, #0xc764'), (0xc764, 'ldr', 'w24, [x21]'),
        (0xc768, 'mov', 'w25, #1'), (0xc780, 'tbz', 'w25, #0, #0xc794'),
        (0xc784, 'mov', 'x8, xzr'), (0xc788, 'stp', 'xzr, xzr, [sp, #0x30]'),
        (0xc78c, 'str', 'wzr, [sp, #0x48]'),
        (0xc7d8, 'str', 'x8, [x12]'), (0xc7e0, 'str', 'x15, [x12, #8]'),
        (0xc7f0, 'str', 'w10, [x12, #0x18]'), (0xc82c, 'bl', '#0xd720'),
        (0xd738, 'ldp', 'x23, x22, [x3]'), (0xd778, 'ldr', 'w21, [x21, #0x80]'),
        (0xd7a0, 'ldr', 'x9, [x20, #8]'), (0xd7a4, 'ldr', 'x10, [x19, #8]'),
        (0xd7a8, 'cmp', 'x9, x10'), (0xd7ac, 'b.ne', '#0xd7d0'),
        (0xd7e0, 'add', 'x1, x23, #0x90'), (0xd7e8, 'bl', '#0xd270'),
        (0xd808, 'add', 'x1, x23, #0x70'), (0xd810, 'bl', '#0xd270'),
        (0xd814, 'add', 'x1, x22, #0x50'), (0xd82c, 'b', '#0xd270'),
        (0xd270, 'ldrb', 'w8, [x1, #0x18]'), (0xd274, 'cbz', 'w8, #0xd27c'),
        (0xd284, 'str', 'x8, [x1]'), (0xd288, 'str', 'x1, [x0]'),
        (0xd28c, 'strb', 'w9, [x1, #0x18]'),
        (0xc864, 'cmp', 'w12, #0x400'), (0xc8cc, 'sub', 'w13, w10, #1'),
        (0xc8d0, 'str', 'w13, [x20, #8]'), (0xc8f0, 'bl', '#0xdb80'),
        (0xc924, 'bl', '#0xdb80'), (0xc928, 'bl', '#0xdd70'),
        (0xc948, 'ldr', 'x20, [x19, #0x28]'),
        (0xc99c, 'bl', '#0xd8ec'), (0xc9bc, 'bl', '#0xd8ec'),
        (0xc9f0, 'bl', '#0x9338'), (0xc9f4, 'str', 'xzr, [x19, #0x28]'))
    decoded = CRYPTO.instructions(blob, segments, reads, CLOCK_COMPLETE_RANGES, checks)
    return {'plugin': {'table': '0x432a0', 'update': '0xc704', 'destroy': '0xc930'},
            'pending_zero': 'Length>>3 == 0 selects all configured routes of this client and zero IB/AB/latency, preserving the client and other client records',
            'propagation': '0xd720 compares new/old route records and queues changed master/slave nodes via deduplicating 0xd270; 0xdb80 aggregates/submits',
            'destroy': 'Separate plugin callback 0xc930 removes node records and frees client data; completion does not take this path',
            'instructions': decoded,
            'limitations': ['Static bytes only; no live client count, resulting BCM vote or ACK is captured',
                            'Zero this client request does not imply zero global bandwidth or ARC rails',
                            'Aggregation/submission is covered separately by the crypto ICB verifier',
                            'No final-writer inventory, XNU lease, resource backend or SMMU setup is established']}


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
            ('usb', USB_GUID, USB_SHA, USB_SOURCE, 'usb-resource-handoff-v2', usb_exit),
            ('display', DISPLAY_GUID, DISPLAY_SHA, DISPLAY_SOURCE,
             'display-resource-handoff-final', display_exit),
            ('npa', NPA_GUID, NPA_SHA, NPA_SOURCE, 'npa-request-complete-v2', npa_complete),
            ('clock', CLOCK_GUID, CLOCK_SHA, CLOCK_SOURCE,
             'clock-request-complete-v2', clock_complete)):
        original = (root / source).read_bytes()
        identity_extra = bind_pe(inner, original, guid, digest, source, name)
        evidence = (root / ('logs/pas13-instruction-replay-20261110/' +
                           evidence_name + '-evidence.json')).read_bytes()
        extra_segments, extra_reads = ICB.checked_reads(original, digest, json.loads(evidence))
        extras[name] = {'identity': identity_extra,
                        'evidence_sha256': hashlib.sha256(evidence).hexdigest(),
                        'read_evidence_ids': list(extra_reads.values()),
                        ('request_completion' if name in ('npa', 'clock') else 'exit_path'):
                        analyze(original, extra_segments, extra_reads)}
    return {'status': 'PASS_STATIC_BYTES_ONLY', 'boot_sha256': accepted['boot_sha256'],
            'dxe_core': identity, 'evidence_sha256': hashlib.sha256(bundle).hexdigest(),
            'read_evidence_ids': list(reads.values()), 'core_order': core_order(blob, segments, reads),
            'exit_consumers': {name: extras[name] for name in ('darwin', 'usb', 'display')},
            'request_backends': {name: extras[name] for name in ('npa', 'clock')},
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
