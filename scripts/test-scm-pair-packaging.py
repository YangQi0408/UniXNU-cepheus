#!/usr/bin/env python3
"""Exercise real boot packaging with a synthetic kernel fixture, never boot it."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import tempfile


def load(name,file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod


package=load('package','make-scm-pair-boot.py')
verify=load('verify','verify-scm-pair-boot.py')


def refused(callback):
    try:callback()
    except (AssertionError,ValueError,KeyError,struct.error):return
    raise AssertionError('invalid input unexpectedly accepted')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','fd','efi','receipt'):p.add_argument(name,type=Path)
    a=p.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        # A substantial ARM64 header plus markers exercises the packaging
        # contract only. It cannot be confused with a compiled XNU candidate.
        kernel=root/'synthetic-kernel.macho'
        data=bytes.fromhex('cffaedfe0c000001')+bytes(2000000)+b'\0'.join(package.MARKERS)
        kernel.write_bytes(data)
        image=root/'fixture.img'
        package.package(a.baseline,kernel,a.fd,image)
        report=verify.verify(image,a.baseline,kernel,a.fd,a.efi,a.receipt)
        assert report['status']=='PASS'
        altered=root/'altered.img';changed=bytearray(image.read_bytes());changed[576]^=1
        altered.write_bytes(changed)
        refused(lambda:verify.verify(altered,a.baseline,kernel,a.fd,a.efi,a.receipt))
        changed=bytearray(image.read_bytes());changed[16]^=1;altered.write_bytes(changed)
        refused(lambda:verify.verify(altered,a.baseline,kernel,a.fd,a.efi,a.receipt))
        wrong=root/'wrong.macho';wrong.write_bytes(data+b'other')
        refused(lambda:verify.verify(image,a.baseline,wrong,a.fd,a.efi,a.receipt))
        receipt=root/'wrong.json';rec=json.loads(a.receipt.read_text());rec['fd_sha256']='0'*64
        receipt.write_text(json.dumps(rec))
        refused(lambda:verify.verify(image,a.baseline,kernel,a.fd,a.efi,receipt))
        receipt.write_text(json.dumps({**rec,'fd_sha256':verify.sha(a.fd.read_bytes()),'driver_sha256':'0'*64}))
        refused(lambda:verify.verify(image,a.baseline,kernel,a.fd,a.efi,receipt))
        _,_,_,old=verify.unpack(a.baseline.read_bytes(),root/'baseline')
        config=package.paired_config(old[verify.CONFIG])
        verify.validate_config(old[verify.CONFIG],config)
        refused(lambda:verify.validate_config(old[verify.CONFIG],config.replace(b'cephscmpair=1',b'cephscmpair=1 cephsoc=1')))
        refused(lambda:verify.validate_config(old[verify.CONFIG],config.replace(b'sweettouchdelay=60',b'sweettouchdelay=45')))
        slot=package.add_slot(old[verify.KERNEL+'.adt'])
        verify.validate_adt(old[verify.KERNEL+'.adt'],slot)
        refused(lambda:package.add_slot(slot))
        tree,end=verify.adt.parse(slot);assert end==len(slot)
        chosen=next(c for c in tree.children if c.name=='chosen')
        chosen.props=[(n,f,b'1'+v[1:] if n==verify.SLOT else v) for n,f,v in chosen.props]
        refused(lambda:verify.validate_adt(old[verify.KERNEL+'.adt'],verify.adt.serialize(tree)))
        # Real nested FV and exact EFI bytes are parsed independently.
        altered_fd=bytearray(a.fd.read_bytes());altered_fd[48]^=1
        refused(lambda:verify.inner_files(bytes(altered_fd)))
    print('Pair boot packaging and independent verification: end-to-end fixture plus 10 rejection cases PASS; synthetic kernel never booted')


if __name__=='__main__':main()
