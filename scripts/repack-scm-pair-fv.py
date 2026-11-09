#!/usr/bin/env python3
"""Rebuild the accepted firmware volume with only DarwinBoot replaced.

Uses the existing EDK2 FFS/FV recipes, proves baseline reproducibility first,
then verifies each nested FFS file and the actual PE section in the new FD.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import uuid

DRIVER = '1B64AF87-8415-4B57-9B16-680D70913B9F'
WRAPPER = '9E21FD93-9C72-4c15-8C4B-E77F1DB2D792'
LZMA_GUID = 'EE4E5898-3914-4259-9D6E-DC7BD79403CF'
FD_SHA = '511323ca7115e5c9f2d78e32d56c632775450738641ad75e189185959a74bcc4'
OLD_PREFIX = '/work/cepheus/uefi/Build/SurfaceDuo1Pkg/RELEASE_CLANGPDB/FV/'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def files(volume):
    assert len(volume)>=56
    assert volume[40:44]==b'_FVH'
    assert struct.unpack_from('<Q',volume,32)[0]==len(volume)
    header=struct.unpack_from('<H',volume,48)[0]
    assert 56<=header<=len(volume) and header%2==0
    assert sum(struct.unpack('<%dH'%(header//2),volume[:header]))&0xffff==0
    offset=(header+7)&~7
    result={}
    while offset+24<=len(volume):
        if volume[offset:offset+24]==b'\xff'*24:
            assert volume[offset:]==b'\xff'*(len(volume)-offset)
            break
        h=volume[offset:offset+24]
        size=int.from_bytes(h[20:23],'little')
        assert size>=24 and size!=0xffffff and offset+size<=len(volume)
        assert (sum(h)-h[17]-h[23])&255==0
        guid=str(uuid.UUID(bytes_le=h[:16])).upper()
        body=volume[offset:offset+size]
        if h[19]&0x40:assert (sum(body[24:])+h[17])&255==0
        else:assert h[17]==0xaa
        assert guid not in result
        result[guid]=body
        offset=(offset+size+7)&~7
    return result


def sections(data):
    result=[];offset=0
    while offset+4<=len(data):
        size=int.from_bytes(data[offset:offset+3],'little')
        assert 4<=size<0xffffff and offset+size<=len(data)
        result.append((data[offset+3],data[offset+4:offset+size]))
        offset=(offset+size+3)&~3
    assert offset==len(data) or offset-3<=len(data)<offset
    return result


def repack(source,efi,tools,output):
    assert not output.exists()
    baseline=(source/'SM8150_EFI.fd').read_bytes()
    assert digest(baseline)==FD_SHA
    output.mkdir(parents=True)
    def run(name,*args):
        subprocess.run([str(tools/name),*map(str,args)],check=True,stdout=subprocess.DEVNULL)
    def recipe(name,changes):
        text=(source/(name+'.inf')).read_text()
        text=text.replace(OLD_PREFIX,str(source.resolve())+'/')
        for old,new in changes.items():
            assert text.count(str(old))==1
            text=text.replace(str(old),str(new))
        path=output/(name+'.inf');path.write_text(text)
        return path
    # Rebuilding unchanged FFS material must reproduce the accepted FD.
    baseline_inf=recipe('FVMAIN_COMPACT',{})
    control=output/'baseline.fd'
    run('GenFv','-i',baseline_inf,'-o',control)
    assert control.read_bytes()==baseline
    main_inf=recipe('FVMAIN',{})
    control_main=output/'baseline-main.fv'
    run('GenFv','-i',main_inf,'-o',control_main)
    assert control_main.read_bytes()==(source/'FVMAIN.Fv').read_bytes()
    driver_dir=source/'Ffs'/f'{DRIVER}DarwinBoot'
    pe=output/'DarwinBoot.pe32'
    driver=output/'DarwinBoot.ffs'
    run('GenSec','-s','EFI_SECTION_PE32','-o',pe,efi)
    run('GenFfs','-t','EFI_FV_FILETYPE_DRIVER','-g',DRIVER,'-o',driver,
        '-oi',pe,'-oi',driver_dir/f'{DRIVER}SEC3.ui')
    candidate_inf=recipe('FVMAIN',{driver_dir/f'{DRIVER}.ffs':driver})
    main=output/'candidate-main.fv'
    run('GenFv','-i',candidate_inf,'-o',main)
    old_files=files(control_main.read_bytes());new_files=files(main.read_bytes())
    assert old_files.keys()==new_files.keys()
    assert [k for k in old_files if old_files[k]!=new_files[k]]==[DRIVER]
    image_sections=sections(new_files[DRIVER][24:])
    assert [v for t,v in image_sections if t==0x10]==[efi.read_bytes()]
    fvsec=output/'fv.sec';aligned=output/'fv.aligned';compressed=output/'fv.lzma'
    guided=output/'fv.guided';wrapper=output/'wrapper.ffs'
    run('GenSec','-s','EFI_SECTION_FIRMWARE_VOLUME_IMAGE','-o',fvsec,main)
    run('GenSec','--sectionalign','8','-o',aligned,fvsec)
    run('LzmaCompress','-e','-o',compressed,aligned)
    run('GenSec','-s','EFI_SECTION_GUID_DEFINED','-g',LZMA_GUID,'-r','PROCESSING_REQUIRED','-o',guided,compressed)
    run('GenFfs','-t','EFI_FV_FILETYPE_FIRMWARE_VOLUME_IMAGE','-g',WRAPPER,'-o',wrapper,'-i',guided)
    outer_inf=recipe('FVMAIN_COMPACT',{source/'Ffs'/f'{WRAPPER}FVMAIN_COMPACT'/f'{WRAPPER}.ffs':wrapper})
    result=output/'scm-pair.fd'
    run('GenFv','-i',outer_inf,'-o',result)
    old_outer=files(baseline);new_outer=files(result.read_bytes())
    assert old_outer.keys()==new_outer.keys()
    assert [k for k in old_outer if old_outer[k]!=new_outer[k]]==[WRAPPER.upper()]
    outer_key=next(k for k in new_outer if k.lower()==WRAPPER.lower())
    outer_sections=sections(new_outer[outer_key][24:]);assert len(outer_sections)==1
    kind,data=outer_sections[0];assert kind==2 and data[:16]==uuid.UUID(LZMA_GUID).bytes_le
    data_offset,attrs=struct.unpack_from('<HH',data,16);assert attrs==1
    packed=output/'verify.lzma';unpacked=output/'verify.sec'
    packed.write_bytes(data[data_offset-4:]);run('LzmaCompress','-d','-o',unpacked,packed)
    decoded=sections(unpacked.read_bytes())
    # GenSec emits an empty RAW alignment section before the FV image.
    assert decoded==[(0x19,b''),(0x17,main.read_bytes())]
    report={'baseline_fd_sha256':digest(baseline),'baseline_reproduced':True,
        'fd_sha256':digest(result.read_bytes()),'fd_bytes':result.stat().st_size,
        'driver_sha256':digest(efi.read_bytes()),'main_files':len(new_files),
        'only_main_file_changed':DRIVER,'only_outer_file_changed':WRAPPER,
        'actual_pe_extracted_and_equal':True,'phone_operations':False}
    (output/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for arg in ('source','efi','tools','output'):p.add_argument(arg,type=Path)
    a=p.parse_args()
    print(json.dumps(repack(*(getattr(a,k).resolve() for k in ('source','efi','tools','output'))),indent=2))
