#!/usr/bin/env python3
"""Local SMEM packaging fixture; admission is mocked only for this test."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from unittest.mock import patch


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PACK = module('smem_fixture_pack', 'make-scm-pair-boot.py')
VERIFY = module('smem_fixture_verify', 'verify-scm-pair-boot.py')
LOCAL = module('smem_fixture_local', 'build-scm-pair-local.py')


def refused(callback):
    try:
        callback()
    except (AssertionError, ValueError, KeyError, struct.error, subprocess.CalledProcessError):
        return
    raise ValueError('invalid fixture input accepted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'fd', 'efi', 'receipt'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='smem-package-fixture-') as directory:
        root = Path(directory)
        kernel = root / 'synthetic-not-xnu.macho'
        kernel.write_bytes(bytes.fromhex('cffaedfe0c000001') + bytes(2000000) + b'\0'.join(PACK.SMEM_MARKERS))
        output = root / 'synthetic-never-boot.img'
        receipt = root / 'fake-not-admission.json'
        receipt.write_text('{}')
        refused(lambda: PACK.package(args.baseline,kernel,args.fd,output,smem_map=True))
        refused(lambda: PACK.package(args.baseline,kernel,args.fd,output,smem_map=True,kernel_receipt=receipt))
        if output.exists():
            raise ValueError('rejected admission created package')
        digest = hashlib.sha256(kernel.read_bytes()).hexdigest()
        native = {'status':'PASS_LINKED_SMEM_RECEIVER','kernel_sha256':digest}
        report = {'status':'PASS','kernel_sha256':digest,'kernel_bytes':kernel.stat().st_size,
                  'smem_map_abi_source_verified':True,'native_smem_receiver_and_thread_verified':True,
                  'ci_staged_source_comparisons_passed':True,'unresolved_symbols':[],
                  'commit':'fixture-not-ci','run_state':{'headSha':'fixture-not-ci','status':'completed','conclusion':'success'},
                  'source_sha256':{path.name:hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in (PACK.smem.ROOT/'xnu').iterdir()
                                   if path.is_file() and not path.name.startswith('test_')},
                  'smem_map_verification':native,'phone_operations':False,'gpu_execution':False}
        receipt.write_text(json.dumps(report))
        with patch.object(PACK.smem.NATIVE,'verify',return_value=native):
            PACK.smem.validate(kernel,receipt)
            for key,value in (('kernel_sha256','0'*64),('kernel_bytes',1),('smem_map_abi_source_verified',False),
                              ('native_smem_receiver_and_thread_verified',False),('source_sha256',{}),
                              ('run_state',{}),('unresolved_symbols',['_undefined']),('smem_map_verification',{})):
                receipt.write_text(json.dumps({**report,key:value}))
                refused(lambda: PACK.smem.validate(kernel,receipt))
        receipt.write_text(json.dumps(report))
        # Even a complete-looking, fabricated receipt must not admit this
        # marker-only kernel without independent native disassembly.
        refused(lambda: PACK.smem.validate(kernel,receipt))
        command = [sys.executable,'-O',str(Path(__file__).with_name('make-scm-pair-boot.py')),
                   str(args.baseline),str(kernel),str(args.fd),str(output),'--smem-map','--kernel-receipt',str(receipt)]
        optimized = subprocess.run(command,capture_output=True,text=True)
        if optimized.returncode == 0 or 'optimization is refused' not in optimized.stderr or output.exists():
            raise ValueError('optimized assertion-based packaging not refused')
        # Only this fixture replaces admission; production CLI has no bypass.
        with patch.object(PACK.smem,'validate',return_value={}) as admission:
            PACK.package(args.baseline,kernel,args.fd,output,smem_map=True,kernel_receipt=receipt)
            if admission.call_count != 1:
                raise ValueError('packaging missed admission')
        def verify(image=output, candidate=kernel, fv_receipt=args.receipt):
            with patch.object(VERIFY.smem,'validate',return_value={}):
                return VERIFY.verify(image,args.baseline,candidate,args.fd,args.efi,fv_receipt,
                                     smem_map=True,kernel_receipt=receipt)
        result = verify()
        if result['profile'] != 'smem-map-handoff' or result['zero_handoff_bytes'] != 4576:
            raise ValueError('fixture contract differs')
        altered = root / 'altered.img'
        for offset in (576,16):
            data = bytearray(output.read_bytes());data[offset] ^= 1
            altered.write_bytes(data)
            refused(lambda: verify(image=altered))
        wrong = root / 'wrong.macho';wrong.write_bytes(kernel.read_bytes()+b'wrong')
        refused(lambda: verify(candidate=wrong))
        fv_receipt = root / 'wrong-fv.json'
        fv_report = json.loads(args.receipt.read_text());fv_report['driver_sha256'] = '0'*64
        fv_receipt.write_text(json.dumps(fv_report))
        refused(lambda: verify(fv_receipt=fv_receipt))
        _,_,_,files = VERIFY.unpack(args.baseline.read_bytes(),root/'baseline')
        oldconfig = files[VERIFY.CONFIG]
        config = PACK.paired_config(oldconfig,smem_map=True)
        VERIFY.validate_config(oldconfig,config,smem_map=True)
        refused(lambda: VERIFY.validate_config(oldconfig,config.replace(b'cephsmemmap=1',b'cephsmemmap=1 cephsoc=1'),smem_map=True))
        refused(lambda: VERIFY.validate_config(oldconfig,config.replace(b'sweettouchdelay=60',b'sweettouchdelay=45'),smem_map=True))
        oldadt = files[VERIFY.KERNEL+'.adt']
        slot = PACK.add_slot(oldadt,smem_map=True)
        VERIFY.validate_adt(oldadt,slot,smem_map=True)
        refused(lambda: PACK.add_slot(slot,smem_map=True))
        tree,end = VERIFY.adt.parse(slot)
        chosen = next(child for child in tree.children if child.name == 'chosen')
        for size in (4575,4577):
            chosen.props = [(name,flags,bytes(size) if name=='cepheus-smem-map' else data)
                            for name,flags,data in chosen.props]
            refused(lambda: VERIFY.validate_adt(oldadt,VERIFY.adt.serialize(tree),smem_map=True))
        refused(lambda: PACK.add_slot(oldadt,rpmh_tail=True,smem_map=True))
        argv = ['build-scm-pair-local.py','--kernel',str(kernel),'--kernel-receipt',str(receipt),
                '--baseline',str(args.baseline),'--output',str(root/'local-fixture'),
                '--smem-map','--firmware-build',str(root/'loader')]
        loader_efi = root/'loader/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
        loader_efi.parent.mkdir(parents=True);loader_efi.write_bytes(args.efi.read_bytes())
        receipt.write_text(json.dumps({**report,'native_cpu_capture_and_smc_verified':True}))
        commands = []
        real_run = subprocess.run
        def stub_run(command,**kwargs):
            if command[0] == 'git':
                return real_run(command,**kwargs)
            commands.append(command)
            if 'repack-scm-pair-fv.py' in Path(command[1]).name:
                target = Path(command[-1]);target.mkdir()
                (target/'scm-pair.fd').write_bytes(args.fd.read_bytes())
            if 'make-scm-pair-boot.py' in Path(command[1]).name:
                Path(command[5]).write_bytes(b'FIXTURE NEVER BOOT')
            return subprocess.CompletedProcess(command,0)
        with patch.object(sys,'argv',argv),patch.object(LOCAL.smem,'validate',return_value={}),patch.object(LOCAL.subprocess,'run',side_effect=stub_run):
            if LOCAL.main() != 0 or len(commands) != 4:
                raise ValueError('local fixture step order')
        if any('--core' in command for command in commands) or '--smem-map' not in commands[2] or '--smem-map' not in commands[3]:
            raise ValueError('local fixture mixed mode/core replacement')
        for options in (['--rpmh-tail'],['--core-build',str(root/'core')]):
            with patch.object(sys,'argv',[*argv[:7],'--output',str(root/'refused-local'),
                                         '--smem-map','--firmware-build',str(root/'loader'),*options]):
                refused(LOCAL.main)
    print('SMEM packaging fixture/admission rejection/native FV equality PASS; synthetic kernel and mocked admission, never booted')


if __name__ == '__main__':
    main()
