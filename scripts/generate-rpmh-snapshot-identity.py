#!/usr/bin/env python3
"""Generate private byte identities from pinned local PE artifacts, never execute them."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('pe_ranges', ROOT/'scripts/verify-npa-state-evidence.py')
PE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PE)
SOURCES = (
    ('Rpmh', 'uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/RpmhDxe/RpmhDxe.efi',
     '03ccd266ce0c9e268ac199c0f27903c9dbdac675c9374c797e4178673564a7fa'),
    ('Dal', 'uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/PatchedBinaries/DALSYSDxe.efi',
     '97231a87c321ed3d764f97b2f687b0f29c21787c56f013aadf0f01aa50effcbd'),
    ('CmdDb', 'uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.efi',
     'f1fe60c53ca0eb2613cec20aa6c69d8375543c76bbc226ba3a8c41bfc5535849'),
    ('Core', 'uefi/Build/SurfaceDuo1Pkg/RELEASE_CLANGPDB/AARCH64/DxeCore.efi',
     '311a20a50632dbabdfcdaa9bc2ed7d08e4712c2c70b8f3f18b66834803812be8'),
)


def identity(blob, digest):
    if hashlib.sha256(blob).hexdigest() != digest:
        raise ValueError('pinned PE identity mismatch')
    segments = PE.pe_segments(blob)
    pe = struct.unpack_from('<I', blob, 60)[0]
    count, optional = struct.unpack_from('<H', blob, pe+6)[0], struct.unpack_from('<H', blob, pe+20)[0]
    size, headers = struct.unpack_from('<II', blob, pe+24+56)
    sections = [struct.unpack_from('<8sIIIIIIHHI', blob, pe+24+optional+40*i) for i in range(count)]
    texts = [s for s in sections if s[0].rstrip(b'\0') == b'.text']
    if len(texts) != 1 or headers > 4096 or headers > len(blob):
        raise ValueError('unexpected PE layout')
    text = texts[0]
    if text[1] > text[3] or text[2] % 8 or text[1] % 8:
        raise ValueError('unexpected text alignment')
    raw = PE.span(blob, segments, text[2], text[1])
    reloc_rva, reloc_size = struct.unpack_from('<II', blob, pe+24+112+5*8)
    reloc = PE.span(blob, segments, reloc_rva, reloc_size)
    fixups = []
    cursor = 0
    while cursor < len(reloc):
        if not any(reloc[cursor:]):
            break
        page, block = struct.unpack_from('<II', reloc, cursor)
        if block < 8 or block % 2 or block > len(reloc)-cursor:
            raise ValueError('invalid reloc block')
        for (entry,) in struct.iter_unpack('<H', reloc[cursor+8:cursor+block]):
            kind, address = entry >> 12, page+(entry & 4095)
            if not kind:
                continue
            if kind != 10:
                raise ValueError('non-DIR64 fixup')
            if text[2] <= address < text[2]+len(raw):
                if address % 8 or address+8 > text[2]+len(raw):
                    raise ValueError('unaligned text fixup')
                fixups.append((address-text[2])//8)
        cursor += block
    if len(fixups) != len(set(fixups)):
        raise ValueError('duplicate text fixup')
    return dict(image_size=size, header=blob[:headers], base_offset=pe+48,
                text_rva=text[2], text=raw, fixups=sorted(fixups))


def generate(root, output):
    if output.exists():
        raise ValueError('output already exists')
    lines = ['/* Generated from pinned private artifacts; do not commit. */',
             '#ifndef CEPHEUS_RPMH_SNAPSHOT_IDENTITY_H', '#define CEPHEUS_RPMH_SNAPSHOT_IDENTITY_H']
    for name, path, digest in SOURCES:
        item = identity((root/path).read_bytes(), digest)
        lines += [f'/* {name}: SHA256 {digest} */', f'#define CE_{name.upper()}_IMAGE_BYTES {item["image_size"]}u',
                  f'#define CE_{name.upper()}_TEXT_RVA {item["text_rva"]}u',
                  f'#define CE_{name.upper()}_BASE_OFFSET {item["base_offset"]}u',
                  f'STATIC CONST UINT8 mCe{name}Header[] = {{']
        for off in range(0, len(item['header']), 16):
            lines.append('  '+','.join(f'0x{x:02x}' for x in item['header'][off:off+16])+',')
        lines += ['};', f'STATIC CONST UINT64 mCe{name}Text[] = {{']
        words = [x[0] for x in struct.iter_unpack('<Q', item['text'])]
        for off in range(0, len(words), 4):
            lines.append('  '+','.join(f'UINT64_C(0x{x:016x})' for x in words[off:off+4])+',')
        lines += ['};', f'STATIC CONST UINT32 mCe{name}Fixups[] = {{'+
                  ','.join([*(str(x) for x in item['fixups']), 'UINT32_MAX'])+'};']
    lines += ['#endif', '']
    output.write_text('\n'.join(lines))
    return {name: digest for name, _, digest in SOURCES}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(generate(args.root.resolve(), args.output.resolve()))
