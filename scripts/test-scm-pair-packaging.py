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
    p.add_argument('--core',type=Path,help='exercise exit-tail packaging with both compiled EFI replacements')
    a=p.parse_args()
    tail=a.core is not None
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        # A substantial ARM64 header plus markers exercises the packaging
        # contract only. It cannot be confused with a compiled XNU candidate.
        kernel=root/'synthetic-kernel.macho'
        data=bytes.fromhex('cffaedfe0c000001')+bytes(2000000)+b'\0'.join(package.TAIL_MARKERS if tail else package.MARKERS)
        kernel.write_bytes(data)
        image=root/'fixture.img'
        package.package(a.baseline,kernel,a.fd,image,rpmh_tail=tail)
        def check(image=image,kernel=kernel,receipt=a.receipt,core=a.core):
            return verify.verify(image,a.baseline,kernel,a.fd,a.efi,receipt,core=core)
        report=check()
        assert report['status']=='PASS'
        altered=root/'altered.img';changed=bytearray(image.read_bytes());changed[576]^=1
        altered.write_bytes(changed)
        refused(lambda:check(image=altered))
        changed=bytearray(image.read_bytes());changed[16]^=1;altered.write_bytes(changed)
        refused(lambda:check(image=altered))
        wrong=root/'wrong.macho';wrong.write_bytes(data+b'other')
        refused(lambda:check(kernel=wrong))
        receipt=root/'wrong.json';rec=json.loads(a.receipt.read_text());rec['fd_sha256']='0'*64
        receipt.write_text(json.dumps(rec))
        refused(lambda:check(receipt=receipt))
        receipt.write_text(json.dumps({**rec,'fd_sha256':verify.sha(a.fd.read_bytes()),'driver_sha256':'0'*64}))
        refused(lambda:check(receipt=receipt))
        if tail:
            wrong_core=root/'wrong-core.efi';wrong_core.write_bytes(a.core.read_bytes()+b'wrong')
            refused(lambda:check(core=wrong_core))
            refused(lambda:check(core=None))
        _,_,_,old=verify.unpack(a.baseline.read_bytes(),root/'baseline')
        config=package.paired_config(old[verify.CONFIG],rpmh_tail=tail)
        verify.validate_config(old[verify.CONFIG],config,rpmh_tail=tail)
        profile=b'cephrpmhtail=1' if tail else b'cephscmpair=1'
        refused(lambda:verify.validate_config(old[verify.CONFIG],config.replace(profile,profile+b' cephsoc=1'),rpmh_tail=tail))
        refused(lambda:verify.validate_config(old[verify.CONFIG],config.replace(b'sweettouchdelay=60',b'sweettouchdelay=45'),rpmh_tail=tail))
        slot=package.add_slot(old[verify.KERNEL+'.adt'],rpmh_tail=tail)
        verify.validate_adt(old[verify.KERNEL+'.adt'],slot,rpmh_tail=tail)
        refused(lambda:package.add_slot(slot,rpmh_tail=tail))
        tree,end=verify.adt.parse(slot);assert end==len(slot)
        chosen=next(c for c in tree.children if c.name=='chosen')
        slot_name='cepheus-rpmh-tail' if tail else verify.SLOT
        chosen.props=[(n,f,b'1'+v[1:] if n==slot_name else v) for n,f,v in chosen.props]
        refused(lambda:verify.validate_adt(old[verify.KERNEL+'.adt'],verify.adt.serialize(tree),rpmh_tail=tail))
        # Real nested FV and exact EFI bytes are parsed independently.
        altered_fd=bytearray(a.fd.read_bytes());altered_fd[48]^=1
        refused(lambda:verify.inner_files(bytes(altered_fd)))
    print(('Exit-tail' if tail else 'Pair')+' boot packaging and independent verification: end-to-end fixture and rejection cases PASS; synthetic kernel never booted')


if __name__=='__main__':main()
