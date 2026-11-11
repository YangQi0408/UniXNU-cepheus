#!/usr/bin/env python3
"""Run the actual staged DarwinBoot parser over the real packaged ADT offline."""
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
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'scripts'/file)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


PACK = module('smem_real_adt_package','make-scm-pair-boot.py')


def require(condition,reason):
    if not condition:
        raise ValueError(reason)


def verify(data,config,build):
    source=build/'source/DarwinBootPkg/Driver/DarwinBoot.c'
    manifest=json.loads((build/'source/smem-handoff-source.json').read_text())['files']
    for name in ('DarwinBoot.c','WindowSize.h','cepheus_smem_map_handoff.h'):
        file=source.parent/name
        require(hashlib.sha256(file.read_bytes()).hexdigest()==manifest.get('DarwinBootPkg/Driver/'+name),
                'packaged parser staging differs: '+name)
    text=source.read_text()
    parser='STATIC\nBOOLEAN\nAdtNode'+text.split('STATIC\nBOOLEAN\nAdtNode',1)[1].split('STATIC\nBOOLEAN\nValidTrustCaches',1)[0]
    preamble=next(ast.literal_eval(node.value) for node in ast.parse((ROOT/'scripts/test-scm-pair.py').read_text()).body
        if isinstance(node,ast.Assign) and any(isinstance(target,ast.Name) and target.id=='PREAMBLE' for target in node.targets))
    preamble=preamble.replace('cepheus_scm_handoff.h','cepheus_smem_map_handoff.h')
    preamble=preamble.replace('static int DarwinWindowSizeValid(uint64_t n) {return n==0x10000000;}',
                              '#include "WindowSize.h"')
    wrapper='''
int check(const UINT8 *p,UINTN n,UINT64 *out) {
 UINTN o=0,r=MAX_UINTN,t=MAX_UINTN,s=MAX_UINTN;UINT64 w=0;BOOLEAN packed=0;
 int valid=AdtNode(p,n,&o,0,3,&r,&t,&w,&packed,&s) && o==n;
 out[0]=r;out[1]=t;out[2]=s;out[3]=w;out[4]=packed;return valid;
}
'''
    with tempfile.TemporaryDirectory(prefix='smem-real-adt-') as directory:
        work=Path(directory);c=work/'parser.c';library=work/'parser.so'
        c.write_text(preamble+parser+wrapper)
        subprocess.run(['cc','-shared','-fPIC','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                        '-I',str(build/'source/DarwinBootPkg/Driver'),str(c),'-o',str(library)],check=True)
        parse=ctypes.CDLL(str(library)).check
        parse.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(ctypes.c_uint64)]
        output=(ctypes.c_uint64*5)()
        # The actual XNU receiver also accepts an unaligned property; parse the
        # tree from an intentionally unaligned host buffer here.
        buffer=ctypes.create_string_buffer(b'X'+data)
        require(parse(ctypes.addressof(buffer)+1,len(data),output)==1,'actual parser rejects packaged ADT')
        ram,trust,slot,window,packed=output
        require(slot!=2**64-1 and slot+4576<=len(data) and data[slot:slot+4576]==bytes(4576),
                'actual parser did not identify unique zero slot')
        entry=tomllib.loads(config.decode())['entry'][0]
        require([token for token in entry['args'].split() if token.startswith('ceph')]==['cephsmemmap=1'],
                'foreign packaged profile')
        require('ramdisk' not in entry and ram==trust==2**64-1 and not packed,
                'accepted baseline has no RAMDisk/TrustCache request or destination')
        require(window==0xc0000000, 'accepted baseline window differs')
        bad=[]
        damaged=bytearray(data);damaged[slot]=1;bad.append(bytes(damaged))
        for size in (4575,4577,0x80000000|4576):
            damaged=bytearray(data);struct.pack_into('<I',damaged,slot-4,size);bad.append(bytes(damaged))
        bad.extend((data[:slot+4575],data+b'junk'))
        for candidate in bad:
            backing=ctypes.create_string_buffer(candidate)
            require(parse(backing,len(candidate),output)==0,'actual parser accepts damaged full ADT')
    return {'status':'PASS_ACTUAL_PACKAGED_ADT_PARSER','adt_bytes':len(data),
            'adt_sha256':hashlib.sha256(data).hexdigest(),'parser_source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
            'window_header_sha256':hashlib.sha256((source.parent/'WindowSize.h').read_bytes()).hexdigest(),
            'ramdisk_offset':ram,'trustcache_offset':trust,'smem_offset':slot,'kernel_window':window,
            'packed_ramdisk':bool(packed),'rejection_cases':len(bad),'unaligned_host_buffer':True,
            'phone_operations':False,'firmware_execution':False,'gpu_execution':False}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--build',type=Path,required=True)
    p.add_argument('--adt',type=Path)
    p.add_argument('--config',type=Path)
    p.add_argument('--image',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();require(__debug__,'legacy pack parser requires normal Python')
    require(not a.output.exists(),'preserve prior receipt')
    baseline=a.baseline.read_bytes();require(PACK.digest(baseline)==PACK.BASE_SHA,'foreign baseline')
    _,_,_,fat=PACK.boot_parts(baseline)
    with tempfile.TemporaryDirectory(prefix='smem-baseline-adt-') as directory:
        _,files=PACK.fat_files(fat,Path(directory)/'baseline')
    require(bool(a.image)!=bool(a.adt) and bool(a.adt)==bool(a.config),'supply image OR both ADT/config')
    if a.image:
        _,_,_,newfat=PACK.boot_parts(a.image.read_bytes())
        with tempfile.TemporaryDirectory(prefix='smem-candidate-adt-') as directory:
            _,new=PACK.fat_files(newfat,Path(directory)/'candidate')
        data=new['UniXNU/Payloads/kernel.macho.adt'];config=new['UniXNU/unixnu.toml']
    else:
        data=a.adt.read_bytes();config=a.config.read_bytes()
    require(data==PACK.add_slot(files['UniXNU/Payloads/kernel.macho.adt'],smem_map=True),'not exact packaged baseline ADT')
    require(config==PACK.paired_config(files['UniXNU/unixnu.toml'],smem_map=True),'not exact packaged baseline config')
    result=verify(data,config,a.build.resolve())
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x') as stream:stream.write(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    main()
