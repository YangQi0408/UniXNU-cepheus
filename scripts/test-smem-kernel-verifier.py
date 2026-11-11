#!/usr/bin/env python3
"""Exercise copied-receiver disassembly rejection, then compile its actual body."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('smem_kernel', ROOT / 'scripts/verify-smem-map-kernel.py')
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def main():
    # Synthetic disassembly tests rejection mechanics only, not XNU acceptance.
    assembly = '''0000000000001000 <_soc_smem_map>:
1000: bl 0x2000 <_PE_boot_args>
1004: bl 0x2004 <_swait>
1008: bl 0x2008 <_SecureDTLookupEntry>
100c: bl 0x200c <_SecureDTGetProperty>
1010: bl 0x2010 <_printf>
1014: ret

0000000000001100 <_soc_thread>:
1100: bl 0x1000 <_soc_smem_map>
1104: ret
'''
    VERIFY.inspect(assembly)
    VERIFY.inspect(assembly.replace('1014: ret','1014: bl 0x2100 <_memmove>\n1018: ret'))
    checked_copy = assembly.replace('1014: ret', '1014: bl 0x1200 <___memcpy_chk>\n1018: ret') + '''
0000000000001200 <___memcpy_chk>:
1200: cmp x3, x2
1204: b.lo 0x120c <___memcpy_chk+0xc>
1208: b 0x1300 <_memmove>
120c: sub sp, sp, #0x10
1210: bl 0x1400 <_panic>

0000000000001300 <_memmove>:
1300: ldrb w8, [x1]
1304: strb w8, [x0]
1308: ret
'''
    VERIFY.inspect(checked_copy)
    bad = [assembly.replace('1014: ret', '1014: smc #0'),
           assembly.replace('1014: ret', '1014: blr x8'),
           assembly.replace('<_printf>', '<_gpu_probe>'),
           assembly.replace('<_soc_smem_map>', '<_other>'),
           assembly.replace('bl 0x2010 <_printf>', 'bl 0x2010'),
           assembly + assembly,
           assembly.replace('1014: ret', '1014: b 0x3000 <_gpu_probe>'),
           assembly.replace('1014: ret', '1014: b 0x3000'),
           assembly.replace('1100: bl 0x1000', '1100: adrp x0, 0x1000'),
           assembly.replace('1014: ret', '1014: braaz x8'),
           assembly.replace('1014: ret', '1014: b.eq 0x3000 <_printf>')]
    bad.extend([checked_copy.replace('cmp x3, x2', 'cmp x2, x3'),
                checked_copy.replace('b.lo 0x120c', 'b.hs 0x120c'),
                checked_copy.replace('<___memcpy_chk+0xc>', '<___memcpy_chk+0x8>'),
                checked_copy.replace('1208: b 0x1300 <_memmove>', '1208: b 0x1300 <_gpu_probe>'),
                checked_copy.replace('1210: bl 0x1400 <_panic>', '1210: ret'),
                checked_copy.replace('120c: sub sp, sp, #0x10', '120c: bl 0x1300 <_memmove>'),
                checked_copy.replace('1308: ret', '1308: smc #0'),
                checked_copy.replace('1308: ret', '1308: bl 0x1400 <_gpu_probe>'),
                checked_copy.split('0000000000001300 <_memmove>:', 1)[0],
                assembly.replace('1014: ret', '1014: bl 0x1400 <_panic>')])
    outlined = assembly.replace('1014: ret', '1014: bl 0x1200 <_ceph_smem_map_valid>') + '''
0000000000001200 <_ceph_smem_map_valid>:
1200: ret
'''
    VERIFY.inspect(outlined)
    outlined_copy = checked_copy.replace('1014: bl 0x1200 <___memcpy_chk>',
                                         '1014: bl 0x1500 <_ceph_smem_map_valid>') + '''
0000000000001500 <_ceph_smem_map_valid>:
1500: b 0x1200 <___memcpy_chk>
'''
    if not VERIFY.inspect(outlined_copy)['checked_copy_helpers_verified']:
        raise ValueError('outlined checked copy was not verified')
    bad.extend(outlined.replace('1200: ret', '1200: ' + instruction)
               for instruction in ('b 0x3000 <_gpu_probe>', 'bl 0x3000', 'blrabz x8', 'smc #0'))
    bad.append(outlined_copy.replace('1308: ret', '1308: smc #0'))
    for text in bad:
        try:
            VERIFY.inspect(text)
        except ValueError:
            continue
        raise ValueError('unsafe synthetic disassembly accepted')
    source = (ROOT / 'xnu/cepheus_soc.c').read_text()
    helper = '__attribute__((noinline)) static bool soc_smem_map(void)' + source.split('static bool soc_smem_map(void)', 1)[1].split('static bool soc_scm_pair(void)', 1)[0]
    prefix = '''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cepheus_smem_map_handoff.h"
typedef void *DTEntry;
#define kSuccess 0
extern char *PE_boot_args(void);
extern int printf(const char *,...);
extern void *memcpy(void *,const void *,size_t);
extern void swait(unsigned);
extern int SecureDTLookupEntry(DTEntry,const char *,DTEntry *);
extern int SecureDTGetProperty(DTEntry,const char *,const void **,unsigned *);
#define memcpy(dst,src,n) __builtin___memcpy_chk(dst,src,n,__builtin_object_size(dst,0))
'''
    with tempfile.TemporaryDirectory(prefix='smem-codegen-') as directory:
        work = Path(directory)
        c = work / 'receiver.c'
        c.write_text(prefix + helper + '\n__attribute__((noinline)) bool soc_thread(void) {volatile bool result=soc_smem_map();return result;}\nvoid fixture_entry(void) {soc_thread();}\n')
        output = work / 'receiver.o'
        subprocess.run(['clang', '--target=arm64-apple-macos11', '-ffreestanding', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I', str(ROOT / 'xnu'), '-c', str(c), '-o', str(output)], check=True)
        stubs = work / 'stubs.c'
        stubs.write_text('''
#include <stddef.h>
void fixture_binder(void) __asm__("dyld_stub_binder");
void fixture_binder(void) {__builtin_trap();}
unsigned long __stack_chk_guard;
__attribute__((noreturn)) void __stack_chk_fail(void) {for(;;) {}}
char *PE_boot_args(void) {return "cephsmemmap=1";}
int printf(const char *format,...) {(void)format;return 0;}
void swait(unsigned time) {(void)time;}
int SecureDTLookupEntry(void *root,const char *name,void **entry) {(void)root;(void)name;(void)entry;return -1;}
int SecureDTGetProperty(void *entry,const char *name,const void **property,unsigned *bytes) {(void)entry;(void)name;(void)property;(void)bytes;return -1;}
void *memcpy(void *destination,const void *source,size_t count) {char *d=destination;const char *s=source;for(size_t i=0;i<count;i++) d[i]=s[i];return destination;}
int memcmp(const void *a,const void *b,size_t count) {const unsigned char *x=a,*y=b;for(size_t i=0;i<count;i++) if(x[i]!=y[i]) return x[i]-y[i];return 0;}
int bcmp(const void *a,const void *b,size_t count) {return memcmp(a,b,count);}
__attribute__((noreturn)) void panic(const char *message,...) {(void)message;for(;;) {}}
/* Test-only helper shape from the accepted XNU link, not a runtime substitute. */
__asm__(".text\\n.p2align 2\\n.globl ___memcpy_chk\\n___memcpy_chk:\\n"
        "cmp x3, x2\\nb.lo 1f\\nb _memcpy\\n1:\\n"
        "sub sp, sp, #0x10\\nbl _panic\\n");
''')
        stub_object = work / 'stubs.o'
        subprocess.run(['clang', '--target=arm64-apple-macos11', '-ffreestanding', '-O2', '-c', str(stubs),
                        '-o', str(stub_object)], check=True)
        linked = work / 'fixture.macho'
        if sys.platform == 'darwin':
            command = ['xcrun', 'clang', '-target', 'arm64-apple-macos11', '-nostdlib', '-Wl,-e,_fixture_entry']
        else:
            command = ['ld64.lld', '-arch', 'arm64', '-platform_version', 'macos', '11.0', '11.0', '-e', '_fixture_entry']
        subprocess.run([*command, str(output), str(stub_object), '-o', str(linked)], check=True)
        listing = subprocess.check_output([VERIFY.objdump_tool(), '-d', '--no-show-raw-insn', str(linked)], text=True)
        report = VERIFY.inspect(listing)
        if '___memcpy_chk' not in report['checked_copy_helpers_verified']:
            raise ValueError('actual fortified receiver fixture did not exercise checked copy')
    print(f'SMEM kernel verifier: {len(bad)} synthetic rejections + actual receiver linked ARM64 fixture PASS; no full kernel/runtime')


if __name__ == '__main__':
    main()
