#!/usr/bin/env python3
"""Compile exact production AT and Device byte accesses for Arm64; never run."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    root=Path(directory)
    source=root/'zap.c'
    source.write_text('''#include "cepheus_zap_cache.h"
#include "cepheus_zap_image.h"
uint64_t zap_at(uintptr_t va) { return ceph_zap_translate_write(va); }
void zap_zero(volatile uint8_t *dst,size_t n) { ceph_zap_device_zero(dst,n); }
void zap_copy(uint8_t *dst,const uint8_t *src,size_t n) { ceph_zap_mdt_copy(dst,src,n); }
''')
    assembly=root/'zap.s'
    subprocess.run(['clang','--target=aarch64-none-elf','-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-ffreestanding','-I',str(ROOT/'xnu'),'-S',str(source),'-o',str(assembly)],check=True)
    text=assembly.read_text()
    def body(name):
        return text.split(name+':',1)[1].split('.Lfunc_end',1)[0]
    translation=body('zap_at').lower()
    assert re.search(r'msr\s+daifset,\s*#3',translation)
    assert re.search(r'at\s+s1e1w,',translation) and 'par_el1' in translation
    assert translation.index('daifset')<translation.index('s1e1w')<translation.index('isb')<translation.index('par_el1')
    assert re.search(r'msr\s+daif,',translation) and translation.rindex('daif')>translation.index('par_el1')
    for name in ('zap_copy','zap_zero'):
        stores=re.findall(r'^\s*(st\w+)\s',body(name),re.M)
        assert stores and set(stores)=={'strb'}, (name,stores)
        assert not re.search(r'^\s*bl\s',body(name),re.M), 'no memcpy/bzero helper for Device memory'
    assert not re.search(r'^\s*(smc|hvc)\s',text,re.M)
    subprocess.run(['clang','--target=aarch64-none-elf','-c',str(assembly),'-o',str(root/'zap.o')],check=True)
print('Production ZAP AArch64 AT/ISB/DAIF and byte-only Device copy/zero PASS; not executed')
