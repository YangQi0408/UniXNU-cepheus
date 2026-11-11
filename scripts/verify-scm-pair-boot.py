#!/usr/bin/env python3
"""Independently inspect the packaged pair boot and nested firmware contents."""
import argparse
import hashlib
import importlib.util
import json
import lzma
from pathlib import Path
import struct
import subprocess
import tempfile
import tomllib
import uuid
import zlib

import adt

spec=importlib.util.spec_from_file_location('smem_boot_evidence',Path(__file__).with_name('smem-packaging-evidence.py'))
smem=importlib.util.module_from_spec(spec);spec.loader.exec_module(smem)

spec=importlib.util.spec_from_file_location('fv',Path(__file__).with_name('repack-scm-pair-fv.py'))
fv=importlib.util.module_from_spec(spec);spec.loader.exec_module(fv)
BASE_SHA='1f986169db84c5a7eec8cd1b208f70811a676aa2bb26a8a5e071b510d663c95d'
KERNEL='UniXNU/Payloads/kernel.macho'
CONFIG='UniXNU/unixnu.toml'
SLOT='cepheus-scm-handoff'


def sha(data):return hashlib.sha256(data).hexdigest()


def unpack(data,directory):
    assert data[:8]==b'ANDROID!'
    size,_,fat_size,_,second,_,_,page=struct.unpack_from('<8I',data,8)
    assert page==4096 and second==0 and struct.unpack_from('<I',data,40)[0]==0
    offset=page+(size+page-1)//page*page
    assert offset+fat_size==len(data)
    payload=data[page:page+size];fat=data[offset:]
    assert data[576:584]==hashlib.sha1(payload+fat+struct.pack('<III',size,fat_size,0)).digest()[:8]
    assert data[page+size:offset]==bytes(offset-page-size)
    stream=zlib.decompressobj(31)
    raw=stream.decompress(payload,0x300081)
    assert stream.eof and len(raw)==0x300080 and not stream.unconsumed_tail
    dtb=stream.unused_data
    assert dtb[:4]==bytes.fromhex('d00dfeed') and struct.unpack_from('>I',dtb,4)[0]==len(dtb)
    directory.mkdir();image=directory/'fat.img';image.write_bytes(fat)
    subprocess.run(['mcopy','-s','-i',str(image),'::/EFI',str(directory)],check=True)
    files={str(p.relative_to(directory/'EFI')):p.read_bytes()
           for p in (directory/'EFI').rglob('*') if p.is_file()}
    return raw[:128],raw[128:],dtb,files


def inner_files(fd):
    outer=fv.files(fd)
    wrapper=outer[fv.WRAPPER.upper()]
    sections=fv.sections(wrapper[24:]);assert len(sections)==1 and sections[0][0]==2
    guided=sections[0][1]
    assert guided[:16]==uuid.UUID(fv.LZMA_GUID).bytes_le
    offset,attrs=struct.unpack_from('<HH',guided,16);assert attrs==1 and offset==24
    decoded=lzma.decompress(guided[offset-4:],format=lzma.FORMAT_ALONE,memlimit=64*1024*1024)
    sections=fv.sections(decoded)
    assert len(sections)==2 and sections[0]==(0x19,b'') and sections[1][0]==0x17
    return outer,fv.files(sections[1][1])


def validate_config(old,new,rpmh_tail=False,smem_map=False):
    if smem_map:smem.require(__debug__ and not rpmh_tail, 'SMEM requires isolated, nonoptimized validation')
    old=tomllib.loads(old.decode());new=tomllib.loads(new.decode())
    assert len(old['entry'])==len(new['entry'])==1
    before=old['entry'][0]['args'].split();after=new['entry'][0]['args'].split()
    assert [t for t in before if t.startswith('ceph')]==['cephzapmem=1']
    profile='cephsmemmap=1' if smem_map else ('cephrpmhtail=1' if rpmh_tail else 'cephscmpair=1')
    assert [t for t in after if t.startswith('ceph')]==[profile]
    assert len(after)==len({t.split('=',1)[0] for t in after})
    assert [t if t!=profile else 'cephzapmem=1' for t in after]==before
    line=new['entry'][0]['args'];assert len(line)<1000
    new['entry'][0]['args']=old['entry'][0]['args'];assert new==old
    return line


def validate_adt(old,new,rpmh_tail=False,smem_map=False):
    if smem_map:smem.require(__debug__ and not rpmh_tail, 'SMEM requires isolated, nonoptimized validation')
    slot='cepheus-smem-map' if smem_map else ('cepheus-rpmh-tail' if rpmh_tail else SLOT)
    size=4576 if smem_map else (1128 if rpmh_tail else 2656)
    tree,end=adt.parse(new);assert end==len(new) and adt.serialize(tree)==new
    slots=[]
    def scan(node,path):
        for name,flags,data in node.props:
            if name in (SLOT,'cepheus-rpmh-tail','cepheus-rpmh-snapshot','cepheus-smem-map'):
                assert name==slot
                slots.append((node,path,flags,data))
        for child in node.children:scan(child,path+'/'+child.name)
    scan(tree,'')
    assert len(slots)==1
    node,path,flags,data=slots[0]
    assert path=='/chosen' and flags==0 and data==bytes(size)
    node.props=[p for p in node.props if p[0]!=slot]
    assert adt.serialize(tree)==old


def verify(image,reference,kernel,fd,efi,receipt,core=None,smem_map=False,kernel_receipt=None):
    rpmh_tail=core is not None
    if smem_map:
        smem.require(not rpmh_tail, 'mixed diagnostic modes')
        smem.validate(kernel,kernel_receipt)
    elif kernel_receipt is not None:
        raise ValueError('kernel receipt option requires SMEM mode')
    before=reference.read_bytes();after=image.read_bytes();assert sha(before)==BASE_SHA
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        oldshim,oldfd,olddtb,old=unpack(before,root/'old')
        shim,newfd,dtb,new=unpack(after,root/'new')
    assert shim==oldshim and dtb==olddtb and newfd==fd.read_bytes()
    assert len(newfd)==len(oldfd)==0x300000
    hbefore=bytearray(before[:4096]);hafter=bytearray(after[:4096])
    for header in (hbefore,hafter):header[8:12]=bytes(4);header[576:584]=bytes(8)
    assert hbefore==hafter
    assert old.keys()==new.keys()
    mutable={KERNEL,KERNEL+'.adt',CONFIG}
    for name in old.keys()-mutable:assert old[name]==new[name],name
    data=kernel.read_bytes();assert data==new[KERNEL]
    assert data[:8]==bytes.fromhex('cffaedfe0c000001') and len(data)>2000000
    markers=smem.NATIVE.MARKERS if smem_map else ((b'CEPHEUS RPMH TAIL: begin',b'CEPHEUS RPMH TAIL: raw=',b'CEPHEUS RPMH TAIL: end',b'cepheus-rpmh-tail') if rpmh_tail else (
             b'CEPHEUS SCM PAIR: UEFI',b'CEPHEUS SCM PAIR: XNU begin',
             b'CEPHEUS SCM PAIR: end',b'cepheus-scm-handoff',b'CEPHEUS SCM %s CPU:'))
    for marker in markers:
        assert marker in data,marker
    line=validate_config(old[CONFIG],new[CONFIG],rpmh_tail,smem_map)
    validate_adt(old[KERNEL+'.adt'],new[KERNEL+'.adt'],rpmh_tail,smem_map)
    report=json.loads(receipt.read_text())
    assert report['baseline_fd_sha256']==sha(oldfd) and report['baseline_reproduced'] is True
    assert report['fd_sha256']==sha(newfd) and report['fd_bytes']==len(newfd)
    pe=efi.read_bytes();assert report['driver_sha256']==sha(pe)
    assert pe[:2]==b'MZ'
    offset=struct.unpack_from('<I',pe,0x3c)[0]
    assert pe[offset:offset+4]==b'PE\0\0' and struct.unpack_from('<H',pe,offset+4)[0]==0xaa64
    assert struct.unpack_from('<H',pe,offset+24)[0]==0x20b
    oldouter,oldinner=inner_files(oldfd);outer,inner=inner_files(newfd)
    assert oldouter.keys()==outer.keys() and oldinner.keys()==inner.keys()
    assert [k for k in outer if oldouter[k]!=outer[k]]==[fv.WRAPPER.upper()]
    changed={k for k in inner if oldinner[k]!=inner[k]}
    assert changed==({fv.DRIVER,fv.CORE} if rpmh_tail else {fv.DRIVER})
    actual=[body for kind,body in fv.sections(inner[fv.DRIVER][24:]) if kind==0x10]
    assert actual==[pe] and len(inner)==report['main_files']==197
    for guid in changed:
        previous=fv.sections(oldinner[guid][24:]);current=fv.sections(inner[guid][24:])
        assert [kind for kind,_ in previous]==[kind for kind,_ in current]
        assert [(k,v) for k,v in previous if k!=0x10]==[(k,v) for k,v in current if k!=0x10]
    if rpmh_tail:
        core_data=core.read_bytes()
        assert report['main_files_changed']==sorted(changed) and report['core_sha256']==sha(core_data)
        assert report['core_filetype_verified'] is True and report['other_sections_unchanged'] is True
        assert inner[fv.CORE][18]==5
        assert [body for kind,body in fv.sections(inner[fv.CORE][24:]) if kind==0x10]==[core_data]
        assert core_data[:2]==b'MZ'
        offset=struct.unpack_from('<I',core_data,0x3c)[0]
        assert core_data[offset:offset+4]==b'PE\0\0' and struct.unpack_from('<H',core_data,offset+4)[0]==0xaa64
    result={'status':'PASS','image':str(image),'bytes':len(after),'sha256':sha(after),
        'baseline_sha256':BASE_SHA,'kernel_sha256':sha(data),'fd_sha256':sha(newfd),
        'efi_sha256':sha(pe),'args':line,'zero_handoff_bytes':4576 if smem_map else (1128 if rpmh_tail else 2656),
        'nested_files_checked':len(inner),'shim_dtb_unchanged':True,
        'other_fat_files_unchanged':True,'actual_embedded_efi_equal':True,
        'phone_operations':False,'gpu_execution':False}
    if rpmh_tail:result.update(profile='rpmh-exit-tail',core_sha256=sha(core_data),actual_embedded_core_equal=True)
    if smem_map:result.update(profile='smem-map-handoff',kernel_receipt_sha256=sha(Path(kernel_receipt).read_bytes()),
        smem_bytes_acquired=False,smem_access_authorized=False,firmware_execution=False)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('image','reference','kernel','fd','efi','receipt','output'):p.add_argument(name,type=Path)
    p.add_argument('--core',type=Path,help='verify the isolated exit-tail profile and exact embedded core')
    p.add_argument('--smem-map',action='store_true')
    p.add_argument('--kernel-receipt',type=Path)
    a=p.parse_args();assert not a.output.exists()
    if a.smem_map:smem.require(__debug__ and not a.output.exists(), 'preserve prior verification; no optimization')
    result=verify(*(getattr(a,n) for n in ('image','reference','kernel','fd','efi','receipt')),core=a.core,
                  smem_map=a.smem_map,kernel_receipt=a.kernel_receipt)
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
