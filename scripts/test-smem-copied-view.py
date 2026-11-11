#!/usr/bin/env python3
"""Compare actual C copied-view parser to Python; no device or physical memory."""
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


FIXTURE = module('smem_fixture', ROOT / 'scripts/test-smem-versions.py')
PY = FIXTURE.MODULE


def require(condition, message):
    if not condition:
        raise ValueError(message)


class Info(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in ('version', 'item_count', 'offset', 'bytes', 'allocations')]


def main():
    with tempfile.TemporaryDirectory(prefix='smem-copied-') as temp:
        work = Path(temp)
        header = ROOT / 'firmware/cepheus_smem_copied_view.h'
        source = work / 'wrapper.c'
        source.write_text('#include "cepheus_smem_copied_view.h"\n'
                          'int extract(unsigned char *out,struct ceph_smem_selection *info,'
                          'const unsigned char *in,unsigned n,unsigned long long base) {'
                          'struct ceph_smem_scratch scratch;'
                          'return ceph_smem_extract(out,info,in,n,base,&scratch);}\n')
        lib = work / 'parser.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=undefined', '-I', str(header.parent), str(source), '-o', str(lib)], check=True)
        parse = ctypes.CDLL(str(lib)).extract
        parse.argtypes = [ctypes.c_void_p, ctypes.POINTER(Info), ctypes.c_void_p,
                          ctypes.c_uint32, ctypes.c_uint64]
        parse.restype = ctypes.c_int
        checked = 0
        corpus = (work / 'corpus.bin').open('wb')

        def compare(blob, base=FIXTURE.BASE):
            nonlocal checked
            original = bytes(blob)
            input_ = ctypes.create_string_buffer(original)
            output = ctypes.create_string_buffer(b'\xa5' * 4096, 4096)
            info = Info(*([0xa5a5a5a5] * 5))
            error = parse(output, ctypes.byref(info), input_, len(original), base)
            try:
                expected, mapping = PY.extract_region(original, base)
            except ValueError:
                corpus.write(struct.pack('<IQI5I', len(original), base, 0, 0, 0, 0, 0, 0))
                corpus.write(original)
                require(error != 0, f'C accepted Python-rejected case {checked}')
                require(output.raw == b'\xa5' * 4096 and bytes(info) == b'\xa5' * 20,
                        'failure published output')
            else:
                corpus.write(struct.pack('<IQI5I', len(original), base, 1, mapping['smem_version'],
                                         mapping['item_count'], mapping['item469']['offset'], 4096,
                                         mapping['global_allocations_scanned']))
                corpus.write(original)
                corpus.write(expected)
                require(error == 0, f'C rejected Python case {checked}: {error}')
                require(output.raw == expected and info.offset == mapping['item469']['offset']
                        and info.bytes == 4096 and info.version == mapping['smem_version']
                        and info.item_count == mapping['item_count']
                        and info.allocations == mapping['global_allocations_scanned'], 'selection mismatch')
            require(input_.raw[:len(original)] == original, 'parser wrote input')
            checked += 1

        for version, cached in ((11, False), (12, False), (12, True)):
            base = FIXTURE.region_fixture(version, cached)
            compare(base)
            for offset in (92, 192, 196, 200, 204, 208 + 469 * 16, FIXTURE.PART,
                           FIXTURE.PART + 8, FIXTURE.PART + 12, FIXTURE.PART + 16,
                           FIXTURE.PART + 32, FIXTURE.PART + 36, FIXTURE.SIZE - 4096,
                           FIXTURE.SIZE - 4092, FIXTURE.SIZE - 4088, FIXTURE.SIZE - 4064,
                           FIXTURE.SIZE - 4060, FIXTURE.SIZE - 4048, FIXTURE.SIZE - 4012):
                for value in (0, 1, 16, 4096, 65536, 0xffffffff):
                    bad = bytearray(base)
                    struct.pack_into('<I', bad, offset, value)
                    compare(bad)
            compare(base, base=FIXTURE.BASE + 4096)
        # Nontrivial layouts exercise every supported padding/cached traversal.
        blob = FIXTURE.region_fixture()
        struct.pack_into('<I', blob, FIXTURE.PART + 12, 32 + 16 + 8 + 4104)
        struct.pack_into('<I', blob, FIXTURE.PART + 36, 4104)
        struct.pack_into('<HH', blob, FIXTURE.PART + 40, 8, 8)
        blob[FIXTURE.PART + 56:FIXTURE.PART + 56 + 4096] = FIXTURE.item_fixture()
        compare(blob)
        blob = FIXTURE.region_fixture(cached=True)
        second = FIXTURE.PART + FIXTURE.PART_SIZE - 64 - 4096 - 64
        struct.pack_into('<HHIHHI', blob, second, 0xa5a5, 468, 64, 0, 0, 0)
        struct.pack_into('<I', blob, FIXTURE.PART + 16, second - 64 - FIXTURE.PART)
        compare(blob)
        struct.pack_into('<H', blob, second + 2, 469)
        compare(blob)
        blob = FIXTURE.region_fixture()
        blob[4096:4096 + FIXTURE.PART_SIZE] = blob[FIXTURE.PART:FIXTURE.PART + FIXTURE.PART_SIZE]
        struct.pack_into('<I', blob, FIXTURE.SIZE - 4064, 4096)
        compare(blob)
        # Exercise the fixed scratch arrays and the highest valid item bit.
        blob = FIXTURE.region_fixture(11)
        blob[208:8400] = bytes(8192)
        cursor = 8400
        for item in range(512):
            size = 4096 if item == 469 else 8
            offset = item * 8 if item < 8 else cursor
            struct.pack_into('<IIII', blob, 208 + item * 16, 1, offset, size, 0)
            if item == 469:
                blob[offset:offset + size] = FIXTURE.item_fixture()
            if item >= 8:
                cursor += size
        compare(blob)
        blob = FIXTURE.region_fixture()
        footer = FIXTURE.SIZE - 4096
        blob[footer:] = bytes(4096)
        struct.pack_into('<4sII', blob, footer, b'$TOC', 1, 84)
        for index in range(84):
            offset = FIXTURE.PART if index == 0 else 208 + index * 32
            size = FIXTURE.PART_SIZE if index == 0 else 32
            hosts = (0xfffe, 0xfffe) if index == 0 else (index, 0xffff)
            struct.pack_into('<IIIHHI', blob, footer + 32 + index * 48,
                             offset, size, 0, *hosts, 64)
            if index:
                struct.pack_into('<4sHHIII', blob, offset, b'$PRT', *hosts, size, 32, size)
        struct.pack_into('<4sIIIH', blob, footer + 32 + 84 * 48,
                         b'SIII', FIXTURE.SIZE, FIXTURE.BASE, 0, 65535)
        second = FIXTURE.PART + 4144
        struct.pack_into('<HHIHHI', blob, second, 0xa5a5, 65534, 8, 0, 0, 0)
        struct.pack_into('<I', blob, FIXTURE.PART + 12, 4144 + 24)
        compare(blob)
        struct.pack_into('<H', blob, second + 2, 65535)
        compare(blob)
        struct.pack_into('<I', blob, footer + 8, 85)
        compare(blob)
        rng = random.Random(46912)
        for _ in range(2048):
            blob = FIXTURE.region_fixture(rng.choice((11, 12)), rng.choice((False, True)))
            for _ in range(rng.randrange(1, 5)):
                blob[rng.randrange(len(blob))] ^= 1 << rng.randrange(8)
            compare(blob)
        for size in (0, 207, 4303, 4304, 8399, 8400, 65535):
            compare(bytes(size))
        corpus.close()
        corpus_binary = work / 'corpus'
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(ROOT / 'firmware/test_smem_corpus.c'),
                        '-o', str(corpus_binary)], check=True)
        subprocess.run([str(corpus_binary), str(work / 'corpus.bin')], check=True)
        binary = work / 'guards'
        subprocess.run(['cc', '-std=c11', '-D_GNU_SOURCE', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(ROOT / 'firmware/test_smem_copied_view.c'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        copy_binary = work / 'copy'
        subprocess.run(['cc', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', str(ROOT / 'firmware/test_smem_copy.c'),
                        '-o', str(copy_binary)], check=True)
        subprocess.run([str(copy_binary)], check=True)
        # Freestanding AArch64 compile proves no host-only API is needed by the parser.
        subprocess.run(['clang', '--target=aarch64-none-elf', '-ffreestanding', '-std=c11',
                        '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(header.parent),
                        '-c', str(source), '-o', str(work / 'parser.o')], check=True)
        source.write_text('#include "cepheus_smem_copy.h"\n'
                          'int copy(unsigned char *out,struct ceph_smem_selection *info,'
                          'unsigned char *a,unsigned char *b,unsigned n,unsigned long long base,'
                          'struct ceph_smem_scratch *s,const struct ceph_smem_copy_ops *ops,int phase) {'
                          'return ceph_smem_copy(out,info,a,b,n,base,s,ops,phase);}\n')
        subprocess.run(['clang', '--target=aarch64-none-elf', '-ffreestanding', '-std=c11',
                        '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(header.parent),
                        '-c', str(source), '-o', str(work / 'copy.o')], check=True)
        print(json.dumps({'status': 'PASS', 'differential_cases': checked,
                          'header_sha256': hashlib.sha256(header.read_bytes()).hexdigest(),
                          'host_ubsan': True, 'guards_asan_ubsan': True,
                          'full_corpus_asan_ubsan': True,
                          'aarch64_freestanding_compile': True, 'phone_operations': False,
                          'copy_callback_lifecycle': 'mocked, no live adapter',
                          'gpu_execution': False}))


if __name__ == '__main__':
    main()
