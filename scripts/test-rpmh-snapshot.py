#!/usr/bin/env python3
"""Test actual staged ADT, firmware RAM guards and pinned relocated PE identities."""
import argparse
import ast
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


STAGE = module('snapshot_stage', ROOT/'scripts/stage-scm-pair-firmware.py')
PAIR_PREAMBLE = next(ast.literal_eval(node.value) for node in
    ast.parse((ROOT/'scripts/test-scm-pair.py').read_text()).body
    if isinstance(node,ast.Assign) and any(isinstance(target,ast.Name) and
        target.id=='PREAMBLE' for target in node.targets))
IDENTITY = module('snapshot_identity', ROOT/'scripts/generate-rpmh-snapshot-identity.py')
ADT = module('snapshot_adt', ROOT/'scripts/adt.py')

PREAMBLE = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#define STATIC static
#define CONST const
#define VOID void
#define EFIAPI
#define MAX_UINT64 UINT64_MAX
#define MAX_UINT32 UINT32_MAX
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef char CHAR8;
typedef bool BOOLEAN;
typedef void *EFI_HANDLE;
typedef uint64_t EFI_STATUS;
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} EFI_GUID;
typedef struct {uint32_t Type,Pad;uint64_t PhysicalStart,VirtualStart,NumberOfPages,Attribute;} EFI_MEMORY_DESCRIPTOR;
typedef struct {void *ImageBase;uint64_t ImageSize;} EFI_LOADED_IMAGE_PROTOCOL;
typedef struct {
 EFI_STATUS (*LocateProtocol)(EFI_GUID *,void *,void **);
 EFI_STATUS (*LocateHandleBuffer)(int,EFI_GUID *,void *,UINTN *,EFI_HANDLE **);
 EFI_STATUS (*HandleProtocol)(EFI_HANDLE,EFI_GUID *,void **);
 EFI_STATUS (*AllocatePool)(int,UINTN,void **);
 EFI_STATUS (*FreePool)(void *);
} EFI_BOOT_SERVICES;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_GUID gEfiLoadedImageProtocolGuid;
extern void FreePool(void *);
extern uint64_t DarwinCurrentEl(void);
#define READ_SYSREG(n) 0
#define TRUE true
#define FALSE false
#define ByProtocol 2
#define EfiBootServicesCode 3
#define EfiBootServicesData 4
#define EFI_MEMORY_WB 8
#define EFI_MEMORY_RP 0x2000
#define EFI_ERROR(n) ((int64_t)(n)<0)
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem memcpy
'''


def tree(path='chosen', value=bytes(1016), duplicate=False, pair=False):
    root, chosen = ADT.Node(), ADT.Node()
    root.props=[('name',0,b'device-tree\0')]
    chosen.props=[('name',0,path.encode()+b'\0'),('cepheus-rpmh-snapshot',0,value)]
    if duplicate:
        chosen.props.append(('cepheus-rpmh-snapshot',0,value))
    if pair:
        chosen.props.append(('cepheus-scm-handoff',0,bytes(2656)))
    root.children=[chosen]
    return ADT.serialize(root)


def main(build=None):
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        staged = work/'source'
        STAGE.stage(ROOT.parent/'unixnu/drivers/DarwinBoot', staged, rpmh_snapshot=True)
        driver = staged/'DarwinBootPkg/Driver'
        build_receipt=None
        if build:
            built=build/'source/DarwinBootPkg/Driver'
            for file in driver.iterdir():
                if file.is_file():
                    assert file.read_bytes()==(built/file.name).read_bytes(),file.name
            artifact=build/'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
            blob=artifact.read_bytes()
            IDENTITY.PE.pe_segments(blob)
            build_receipt={'source_matches_tested_stage':True,'artifact':str(artifact.resolve()),
                           'bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest()}
        text = (driver/'DarwinBoot.c').read_text()
        prepare, bind = text.index('CepheusRpmhPrepare ();'), text.index('CepheusRpmhBind (Map')
        assert prepare < text.index('Status = GetMap (&Map, &Capacity, &MapSize') < bind
        last_map = text.index('    Status = gBS->GetMemoryMap (&MapSize')
        ebs = text.index('    Status = gBS->ExitBootServices')
        handoff = text.index('CepheusRpmhHandoff (TreeAddress')
        assert bind < last_map < ebs < handoff < text.index('DarwinEnter (ArgsAddress')
        assert text.index('CepheusRpmhCapture (Map, MapSize, Stride)') > last_map
        assert 'gBS->' not in text[text.index('CepheusRpmhCapture (Map, MapSize, Stride)'):ebs]
        parser = 'STATIC\nBOOLEAN\nAdtNode'+text.split('STATIC\nBOOLEAN\nAdtNode',1)[1].split('STATIC\nBOOLEAN\nValidTrustCaches',1)[0]
        wrapper = r'''
int parse(const UINT8 *p,UINTN n) {
 UINTN o=0,r=MAX_UINTN,t=MAX_UINTN,s=MAX_UINTN,h=MAX_UINTN;UINT64 w=0;BOOLEAN k=0;
 return AdtNode(p,n,&o,0,3,&r,&t,&w,&k,&s,&h) && o==n && h!=MAX_UINTN;
}
'''
        src=work/'parser.c'
        src.write_text(PAIR_PREAMBLE+'#include "cepheus_rpmh_snapshot.h"\n'+parser+wrapper)
        lib=work/'parser.so'
        subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                        '-I',str(ROOT/'xnu'),'-I',str(driver),str(src),'-o',str(lib)],check=True)
        parse=ctypes.CDLL(str(lib)).parse
        parse.argtypes=[ctypes.c_char_p,ctypes.c_size_t]
        good=tree();assert parse(good,len(good))==1
        for bad in (tree('elsewhere'),tree(value=bytes(1015)),tree(value=bytes(1017)),
                    tree(value=b'1'+bytes(1015)),tree(duplicate=True),good+b'junk'):
            assert not parse(bad,len(bad))
        for n in range(len(good)):
            assert not parse(good[:n],n)
        # Mixed profile rejection belongs to boot-profile validation, not parsing.
        print('Actual staged RPMh ADT: exact size/zero/chosen/duplicate/truncation and EBS placement PASS')
        core=work/'reader'
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        str(ROOT/'firmware/test_rpmh_snapshot.c'),'-o',str(core)],check=True)
        subprocess.run([str(core)],check=True)
        firmware=(driver/'CepheusRpmhSnapshot.h').read_text()
        post=firmware.split('CepheusRpmhHandoff (',1)[1]
        for forbidden in ('gBS','GetAddress','LocateProtocol','FreePool','AllocatePool','smc','finish_amc'):
            assert forbidden not in post
        # Host compilation excludes only architecture-specific interrupt masking.
        firmware=firmware.replace('__asm__ volatile ("mrs %0, daif\\n\\tmsr daifset, #0xf\\n\\tdsb sy\\n\\tisb" : "=r"(SavedDaif) : : "memory");','SavedDaif = 0;')
        firmware=firmware.replace('__asm__ volatile ("dsb sy\\n\\tmsr daif, %0\\n\\tisb" : : "r"(SavedDaif) : "memory");','(void)SavedDaif;')
        assert '__asm__' not in firmware
        host=work/'adapter.c'
        wrappers=r'''
static EFI_MEMORY_DESCRIPTOR map[4];
static void setup(uint64_t base,uint64_t pages) {
 memset(map,0,sizeof(map));map[0]=(EFI_MEMORY_DESCRIPTOR){4,0,base,0,pages,8};
 mCepheusRpmhContext.Map=map;mCepheusRpmhContext.MapBytes=sizeof(map[0]);mCepheusRpmhContext.Stride=sizeof(map[0]);
}
void descriptor(unsigned i,uint64_t base,uint64_t bytes) {
 map[i]=(EFI_MEMORY_DESCRIPTOR){4,0,base,0,(bytes+4095)/4096,8};
}
int capture(uint64_t destination,uint64_t rpmh,uint64_t dal,uint64_t cmddb,uint64_t core,uint64_t poolbase) {
 memset(map,0,sizeof(map));
 /* A fifth descriptor is supplied separately below. */
 descriptor(0,rpmh,CE_RPMH_IMAGE_BYTES);descriptor(1,dal,CE_DAL_IMAGE_BYTES);
 descriptor(2,cmddb,CE_CMDDB_IMAGE_BYTES);descriptor(3,core,CE_CORE_IMAGE_BYTES);
 mCepheusRpmhContext.Prepared=1;mCepheusRpmhContext.Address[0]=0x30000;mCepheusRpmhContext.Address[1]=0x30004;
 mCepheusRpmhContext.Image[0]=(CE_RPMH_IMAGE){rpmh,CE_RPMH_IMAGE_BYTES,mCeRpmhHeader,sizeof(mCeRpmhHeader),CE_RPMH_BASE_OFFSET,CE_RPMH_TEXT_RVA,sizeof(mCeRpmhText)/8,mCeRpmhText,mCeRpmhFixups};
 mCepheusRpmhContext.Image[1]=(CE_RPMH_IMAGE){dal,CE_DAL_IMAGE_BYTES,mCeDalHeader,sizeof(mCeDalHeader),CE_DAL_BASE_OFFSET,CE_DAL_TEXT_RVA,sizeof(mCeDalText)/8,mCeDalText,mCeDalFixups};
 mCepheusRpmhContext.Image[2]=(CE_RPMH_IMAGE){cmddb,CE_CMDDB_IMAGE_BYTES,mCeCmdDbHeader,sizeof(mCeCmdDbHeader),CE_CMDDB_BASE_OFFSET,CE_CMDDB_TEXT_RVA,sizeof(mCeCmdDbText)/8,mCeCmdDbText,mCeCmdDbFixups};
 mCepheusRpmhContext.Image[3]=(CE_RPMH_IMAGE){core,CE_CORE_IMAGE_BYTES,mCeCoreHeader,sizeof(mCeCoreHeader),CE_CORE_BASE_OFFSET,CE_CORE_TEXT_RVA,sizeof(mCeCoreText)/8,mCeCoreText,mCeCoreFixups};
 EFI_MEMORY_DESCRIPTOR all[5];memcpy(all,map,sizeof(map));
 all[4]=(EFI_MEMORY_DESCRIPTOR){4,0,poolbase,0,8,8};
 CepheusRpmhCapture(all,sizeof(all),sizeof(all[0]));CepheusRpmhHandoff(destination,0);
 return ceph_rpmh_snapshot_valid((const void *)(uintptr_t)destination) ? -100 : mCepheusRpmhSnapshot.error;
}
int range(uint64_t base,uint64_t pages,uint64_t p,uint32_t n,int type,uint64_t attr) {
 setup(base,pages);map[0].Type=type;map[0].Attribute=attr;
 return CepheusRpmhMapValid(&mCepheusRpmhContext) || CepheusRpmhRamRange(&mCepheusRpmhContext,p,n,0);
}
int malformed(int which) {
 setup(0x100000,16);
 if(which==0)map[0].NumberOfPages=UINT64_MAX;
 if(which==1)map[0].PhysicalStart++;
 if(which==2){map[1]=map[0];mCepheusRpmhContext.MapBytes*=2;}
 if(which==3)mCepheusRpmhContext.Stride=0;
 if(which==4)mCepheusRpmhContext.MapBytes++;
 return CepheusRpmhMapValid(&mCepheusRpmhContext);
}
int image(uint64_t base,uint64_t bytes,unsigned kind) {
 setup(base,(bytes+4095)/4096);
 CE_RPMH_IMAGE i={0};
 switch(kind) {
 case 0:i=(CE_RPMH_IMAGE){base,CE_RPMH_IMAGE_BYTES,mCeRpmhHeader,sizeof(mCeRpmhHeader),CE_RPMH_BASE_OFFSET,CE_RPMH_TEXT_RVA,sizeof(mCeRpmhText)/8,mCeRpmhText,mCeRpmhFixups};break;
 case 1:i=(CE_RPMH_IMAGE){base,CE_DAL_IMAGE_BYTES,mCeDalHeader,sizeof(mCeDalHeader),CE_DAL_BASE_OFFSET,CE_DAL_TEXT_RVA,sizeof(mCeDalText)/8,mCeDalText,mCeDalFixups};break;
 case 2:i=(CE_RPMH_IMAGE){base,CE_CMDDB_IMAGE_BYTES,mCeCmdDbHeader,sizeof(mCeCmdDbHeader),CE_CMDDB_BASE_OFFSET,CE_CMDDB_TEXT_RVA,sizeof(mCeCmdDbText)/8,mCeCmdDbText,mCeCmdDbFixups};break;
 default:i=(CE_RPMH_IMAGE){base,CE_CORE_IMAGE_BYTES,mCeCoreHeader,sizeof(mCeCoreHeader),CE_CORE_BASE_OFFSET,CE_CORE_TEXT_RVA,sizeof(mCeCoreText)/8,mCeCoreText,mCeCoreFixups};break;
 }
 return CepheusRpmhImageValid(&mCepheusRpmhContext,&i);
}
'''
        host.write_text(PREAMBLE+firmware+wrappers)
        lib=work/'adapter.so'
        subprocess.run(['cc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',
                        '-Wno-unused-function','-I',str(driver),str(host),'-o',str(lib)],check=True)
        # Provide the unused firmware-service symbols in a separate stub library.
        stub=work/'stub.c'
        stub.write_text('void *gBS; char gEfiLoadedImageProtocolGuid[16]; void FreePool(void *p){(void)p;} unsigned long long DarwinCurrentEl(void){return 4;}')
        subprocess.run(['cc','-shared','-fPIC',str(stub),'-o',str(work/'stub.so')],check=True)
        ctypes.CDLL(str(work/'stub.so'),mode=ctypes.RTLD_GLOBAL)
        adapter=ctypes.CDLL(str(lib))
        adapter.range.argtypes=[ctypes.c_uint64]*3+[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint64]
        assert not adapter.range(0x100000,16,0x100000,100,4,8)
        assert not adapter.range(0x100000,16,0x100000,100,3,8)
        for args in ((0x100000,16,0xfffff,1,4,8),(0x100000,16,0x110000,1,4,8),
                     (0x100000,16,0x100000,100,11,8),(0x100000,16,0x100000,100,4,0x2008)):
            assert adapter.range(*args)
        for i in range(5):assert adapter.malformed(i)
        adapter.image.argtypes=[ctypes.c_uint64,ctypes.c_uint64,ctypes.c_uint]
        identities=[]
        images=[]
        for kind,(name,path,digest) in enumerate(IDENTITY.SOURCES):
            blob=(ROOT.parent/path).read_bytes();item=IDENTITY.identity(blob,digest)
            size=item['image_size']
            storage=ctypes.create_string_buffer(size+4096)
            base=(ctypes.addressof(storage)+4095)&~4095
            ctypes.memmove(base,item['header'],len(item['header']))
            ctypes.memmove(base+item['text_rva'],item['text'],len(item['text']))
            ctypes.c_uint64.from_address(base+item['base_offset']).value=base
            for off in item['fixups']:
                word=ctypes.c_uint64.from_address(base+item['text_rva']+off*8)
                word.value+=base
            assert not adapter.image(base,size,kind),name
            word=ctypes.c_uint8.from_address(base+item['text_rva']);word.value^=1
            assert adapter.image(base,size,kind),name
            word.value^=1
            if item['fixups']:
                word=ctypes.c_uint64.from_address(base+item['text_rva']+item['fixups'][0]*8)
                word.value-=base
                assert adapter.image(base,size,kind),name
                word.value+=base
            images.append((storage,base))
            identities.append(dict(name=name,sha256=digest,text_bytes=len(item['text']),text_fixups=len(item['fixups'])))
        print('Actual firmware adapter: RAM-only/overlap/overflow/RP guards and four relocated text identities PASS; not executed on phone')
        poolstorage=ctypes.create_string_buffer(32768+4096)
        poolbase=(ctypes.addressof(poolstorage)+4095)&~4095
        def w32(address,value):ctypes.c_uint32.from_address(address).value=value
        def w64(address,value):ctypes.c_uint64.from_address(address).value=value
        def pool_object(address,size):
            total=((size+11)&~7)+40
            w32(address-28,0x30646870);w32(address-20,4);w64(address-12,total)
            w32(address-4,size);w32(address-28+total-16,0x6c617470);w64(address-28+total-8,total)
        array=poolbase+1028;cx=poolbase+2052;mx=poolbase+3076
        pool_object(array,48);pool_object(cx,72);pool_object(mx,72)
        w32(array,0x30000);w64(array+8,cx);w32(array+24,0x30004);w64(array+32,mx)
        w32(cx+24,5);w32(mx+24,6)
        ctypes.c_uint8.from_address(cx+28).value=1;ctypes.c_uint8.from_address(mx+28).value=1
        w32(images[0][1]+0xe644,2);w64(images[0][1]+0xe648,array)
        w64(images[0][1]+0xe610,images[1][1]+0x1f0d0)
        w64(images[1][1]+0x1f160,images[1][1]+0x3390)
        w64(images[1][1]+0x1f168,images[1][1]+0x33c8)
        result=ctypes.create_string_buffer(1016)
        adapter.capture.argtypes=[ctypes.c_uint64]*6
        args=(ctypes.addressof(result),*(base for _,base in images),poolbase)
        assert not adapter.capture(*args)
        assert struct.unpack_from('<I',result.raw,40)[0]==1 # Explicit pre-EBS phase.
        w32(cx-28,0x30726670)
        assert adapter.capture(*args)==-3
        assert result.raw[88:]==bytes(1016-88) # Failure never presents partial votes.
        print('Actual capture -> EBS seal -> ADT copy fixture: two real-layout requests, pre-EBS phase and freed-pool refusal PASS')
        return {'status':'PASS_LOCAL_ONLY','identities':identities,'phone_operations':0,
                'gpu_execution':False,'ownership_or_ack':False,
                'capture_phase':'PRE_EBS_AFTER_FINAL_MAP','post_ebs_pointer_reads':0,
                'firmware_build':build_receipt,
                'files':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted((ROOT/'firmware').glob('*')) if 'rpmh' in p.name.lower() and p.is_file()}}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--firmware-build',type=Path)
    args=parser.parse_args()
    result=main(args.firmware_build)
    if args.output:
        if args.output.exists():raise ValueError('receipt already exists')
        args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
