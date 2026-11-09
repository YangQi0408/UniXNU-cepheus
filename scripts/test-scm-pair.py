#!/usr/bin/env python3
"""Exercise the actual staged ADT parser, pair hook and firmware AArch64 code."""
import ctypes
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


stage = module('stage', ROOT/'scripts/stage-scm-pair-firmware.py')
adt = module('adt', ROOT/'scripts/adt.py')
PREAMBLE = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#define STATIC static
#define CONST const
#define VOID void
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef char CHAR8;
typedef bool BOOLEAN;
#define TRUE true
#define FALSE false
#define MAX_UINTN SIZE_MAX
#define ALIGN_VALUE(x,a) (((x)+(a)-1)&~((a)-1))
#define CompareMem memcmp
#define AsciiStrCmp strcmp
#define ScanMem8(p,n,v) memchr(p,v,n)
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem memcpy
#include "cepheus_scm_handoff.h"
static uint32_t U32(const void *p) {uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t U64(const void *p) {uint64_t v;memcpy(&v,p,8);return v;}
static int DarwinWindowSizeValid(uint64_t n) {return n==0x10000000;}
'''


def tree(path='chosen', value=bytes(2656), duplicate=False):
    root, chosen = adt.Node(), adt.Node()
    root.props=[('name',0,b'device-tree\0')]
    chosen.props=[('name',0,path.encode()+b'\0'),('cepheus-scm-handoff',0,value)]
    if duplicate:
        chosen.props.append(('cepheus-scm-handoff',0,value))
    root.children=[chosen]
    return adt.serialize(root)


with tempfile.TemporaryDirectory() as temp:
    work = Path(temp)
    staged=work/'DarwinBoot'
    stage.stage(ROOT.parent/'unixnu/drivers/DarwinBoot', staged)
    text=(staged/'DarwinBootPkg/Driver/DarwinBoot.c').read_text()
    assert text.count('CepheusScmCapture ();')==1
    assert text.index('CepheusScmCapture ();') < text.index('Status = gBS->ExitBootServices')
    assert text.index('Status = gBS->ExitBootServices') < text.index('CepheusScmHandoff (') < text.index('DarwinEnter (ArgsAddress')
    parser='STATIC\nBOOLEAN\nAdtNode'+text.split('STATIC\nBOOLEAN\nAdtNode',1)[1].split('STATIC\nBOOLEAN\nValidTrustCaches',1)[0]
    wrapper=r'''
int parse(const UINT8 *p, UINTN n) {
    UINTN offset=0,ram=MAX_UINTN,trust=MAX_UINTN,scm=MAX_UINTN;UINT64 window=0;BOOLEAN packed=0;
    return AdtNode(p,n,&offset,0,3,&ram,&trust,&window,&packed,&scm) && offset==n && scm!=MAX_UINTN;
}
'''
    src=work/'parser.c';src.write_text(PREAMBLE+parser+wrapper)
    lib=work/'parser.so'
    subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(ROOT/'xnu'),str(src),'-o',str(lib)],check=True)
    parse=ctypes.CDLL(str(lib)).parse
    parse.argtypes=[ctypes.c_char_p,ctypes.c_size_t]
    good=tree();assert parse(good,len(good))==1
    for bad in (tree('elsewhere'),tree(value=bytes(2655)),tree(value=bytes(2657)),tree(value=b'1'+bytes(2655)),tree(duplicate=True),good+b'junk'):
        assert parse(bad,len(bad))==0
    for n in range(len(good)):
        assert parse(good[:n],n)==0
    print('Actual DarwinBoot ADT parser: exact slot, path, duplicates, size, zero data and all truncations PASS')
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(ROOT/'xnu/test_scm_handoff.c'),'-o',str(work/'handoff')],check=True)
    subprocess.run([str(work/'handoff')],check=True)
    fw=work/'firmware.c'
    fw.write_text(PREAMBLE.split('#include "cepheus_scm_handoff.h"')[0].replace('#include <stdio.h>','').replace('#include <string.h>','').replace('#include <assert.h>','')+r'''
extern void *memset(void *,int,size_t);
extern void *memcpy(void *,const void *,size_t);
extern UINT64 DarwinCurrentEl(void);
#define READ_SYSREG(n) ({UINT64 v;__asm__ volatile("mrs %0, " #n : "=r"(v));v;})
#include "CepheusScmPair.h"
void capture(UINT64 destination) {CepheusScmCapture();CepheusScmHandoff(destination,0);}
''')
    subprocess.run(['clang','--target=aarch64-none-elf','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffreestanding','-I',str(ROOT/'xnu'),'-I',str(ROOT/'firmware'),'-S',str(fw),'-o',str(work/'firmware.s')],check=True)
    assembly=(work/'firmware.s').read_text()
    assert assembly.count('smc\t#0')==1
    for register in ('CurrentEL','MPIDR_EL1','CNTPCT_EL0'):
        assert register in assembly
    print('Actual firmware capture: AArch64 compile, one SMC site, EL/CPU/counter instructions PASS; not executed')
