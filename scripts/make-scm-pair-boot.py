#!/usr/bin/env python3
"""Package the fixed paired SCM diagnostic from the accepted zapmem boot.

Changes DarwinBoot, XNU, the isolated opt-in and one zero ADT slot. Requires
mtools; never accesses the phone. Verify the resulting artifact separately.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import tomllib
import zlib

import adt

BASE_SHA = '1f986169db84c5a7eec8cd1b208f70811a676aa2bb26a8a5e071b510d663c95d'
FD_SHA = '511323ca7115e5c9f2d78e32d56c632775450738641ad75e189185959a74bcc4'
PROPERTY = 'cepheus-scm-handoff'
SLOT_BYTES = 2656
MARKERS = (b'CEPHEUS SCM PAIR: UEFI', b'CEPHEUS SCM PAIR: XNU begin',
           b'CEPHEUS SCM PAIR: end', b'cepheus-scm-handoff', b'CEPHEUS SCM %s CPU:')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def boot_parts(data):
    assert data[:8]==b'ANDROID!'
    k,_,r,_,second,_,_,page=struct.unpack_from('<8I',data,8)
    assert page==4096 and second==0 and struct.unpack_from('<I',data,40)[0]==0
    end=page+((k+page-1)//page)*page
    assert end+r==len(data)
    assert data[576:584]==hashlib.sha1(data[page:page+k]+data[end:end+r]+struct.pack('<III',k,r,0)).digest()[:8]
    stream=zlib.decompressobj(31)
    raw=stream.decompress(data[page:page+k]);assert stream.eof
    assert len(raw)==128+0x300000 and stream.unused_data[:4]==b'\xd0\x0d\xfe\xed'
    return raw[:128],raw[128:],stream.unused_data,data[end:end+r]


def add_slot(data):
    tree,end=adt.parse(data)
    assert end==len(data) and adt.serialize(tree)==data
    def scan(node):
        assert not any(n==PROPERTY for n,_,_ in node.props)
        for child in node.children:scan(child)
    scan(tree)
    chosen=[child for child in tree.children if child.name=='chosen']
    assert len(chosen)==1
    chosen[0].props.append((PROPERTY,0,bytes(SLOT_BYTES)))
    return adt.serialize(tree)


def paired_config(data):
    text=data.decode();config=tomllib.loads(text)
    assert len(config['entry'])==1
    tokens=config['entry'][0]['args'].split()
    assert [t for t in tokens if t.startswith('ceph')]==['cephzapmem=1']
    tokens[tokens.index('cephzapmem=1')]='cephscmpair=1'
    line=' '.join(tokens)
    assert len(line)<1000
    for token in ('sweetusb=2','sweettouch=1','sweettouchgpio=1','sweettouchpower=1',
                  'sweettouchdelay=60','sweettouchdiag=1','sweetkeyspononly=1'):
        assert tokens.count(token)==1
    result,count=re.subn(r'^args = "[^"\n]*"$',f'args = "{line}"',text,flags=re.M)
    assert count==1
    check=tomllib.loads(result)
    check['entry'][0]['args']=config['entry'][0]['args']
    assert check==config
    return result.encode()


def fat_files(data,root):
    root.mkdir()
    image=root/'fat.img';image.write_bytes(data)
    subprocess.run(['mcopy','-s','-i',str(image),'::/EFI',str(root)],check=True)
    return image,{str(p.relative_to(root/'EFI')):p.read_bytes() for p in (root/'EFI').rglob('*') if p.is_file()}


def package(source,kernel,fd,output):
    assert not output.exists()
    before=source.read_bytes();assert digest(before)==BASE_SHA
    shim,oldfd,dtb,fat=boot_parts(before);assert digest(oldfd)==FD_SHA
    newfd=fd.read_bytes();assert len(newfd)==len(oldfd)
    kdata=kernel.read_bytes();assert kdata[:4]==bytes.fromhex('cffaedfe') and len(kdata)>2000000
    assert all(m in kdata for m in MARKERS)
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp);image,original=fat_files(fat,root/'old')
        updates={'UniXNU/unixnu.toml':paired_config(original['UniXNU/unixnu.toml']),
                 'UniXNU/Payloads/kernel.macho.adt':add_slot(original['UniXNU/Payloads/kernel.macho.adt']),
                 'UniXNU/Payloads/kernel.macho':kdata}
        for index,(name,data) in enumerate(updates.items()):
            file=root/str(index);file.write_bytes(data)
            subprocess.run(['mcopy','-o','-i',str(image),str(file),'::/EFI/'+name],check=True)
        newfat=image.read_bytes();assert len(newfat)==len(fat)
        _,actual=fat_files(newfat,root/'new')
        assert actual=={**original,**updates}
    payload=gzip.compress(shim+newfd,mtime=0)+dtb
    header=bytearray(before[:4096]);struct.pack_into('<I',header,8,len(payload))
    header[576:584]=hashlib.sha1(payload+newfat+struct.pack('<III',len(payload),len(newfat),0)).digest()[:8]
    result=bytes(header)+payload+bytes(-len(payload)%4096)+newfat
    check=boot_parts(result);assert check==(shim,newfd,dtb,newfat)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('xb') as f:f.write(result)
    report={'image':str(output),'bytes':len(result),'sha256':digest(result),
        'baseline_sha256':BASE_SHA,'kernel_sha256':digest(kdata),'fd_sha256':digest(newfd),
        'adt_sha256':digest(updates['UniXNU/Payloads/kernel.macho.adt']),
        'args':tomllib.loads(updates['UniXNU/unixnu.toml'].decode())['entry'][0]['args'],
        'shim_and_dtb_unchanged':True,'other_fat_files_unchanged':True,
        'phone_operations':False,'gpu_execution':False}
    output.with_suffix('.packaging.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('source','kernel','fd','output'):parser.add_argument(name,type=Path)
    a=parser.parse_args()
    print(json.dumps(package(a.source,a.kernel,a.fd,a.output),indent=2))
