#!/usr/bin/env python3
"""Verify actual staged tail ADT/parser and native loader build, never hardware."""
import argparse
import ast
from collections import Counter
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result


STAGE=module('tail_loader_stage',ROOT/'scripts/stage-scm-pair-firmware.py')
ADT=module('tail_loader_adt',ROOT/'scripts/adt.py')
PREAMBLE=next(ast.literal_eval(n.value) for n in ast.parse((ROOT/'scripts/test-scm-pair.py').read_text()).body
    if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='PREAMBLE' for t in n.targets))


def tree(path='chosen',size=1128,nonzero=False,duplicate=False):
    root,chosen=ADT.Node(),ADT.Node()
    root.props=[('name',0,b'device-tree\0')]
    value=bytes(size) if not nonzero else b'1'+bytes(size-1)
    chosen.props=[('name',0,path.encode()+b'\0'),('cepheus-rpmh-tail',0,value)]
    if duplicate: chosen.props.append(('cepheus-rpmh-tail',0,value))
    root.children=[chosen];return ADT.serialize(root)


def main(build,output):
    assert not output.exists()
    receipt={'status':'PASS_LOCAL_ONLY','phone_operations':False,'gpu_execution':False,'files':{},'steps':[]}
    with tempfile.TemporaryDirectory() as directory:
        temp=Path(directory);STAGE.stage(ROOT.parent/'unixnu/drivers/DarwinBoot',temp/'source',rpmh_tail=True)
        driver=temp/'source/DarwinBootPkg/Driver';built=build/'source/DarwinBootPkg/Driver'
        for file in driver.iterdir():
            if file.is_file():
                assert file.read_bytes()==(built/file.name).read_bytes(),file.name
                receipt['files'][file.name]=hashlib.sha256(file.read_bytes()).hexdigest()
        text=(driver/'DarwinBoot.c').read_text()
        prepare=text.index('CepheusRpmhTailPrepare ();')
        ebs=text.index('Status = gBS->ExitBootServices')
        handoff=text.index('CepheusRpmhTailHandoff (TreeAddress')
        assert prepare<text.index('Status = GetMap (&Map, &Capacity, &MapSize')<ebs<handoff<text.index('DarwinEnter (ArgsAddress')
        assert 'CepheusRpmhCapture' not in text
        assert 'RpmhProfile == 1' in text
        adapter=(driver/'CepheusRpmhTailHandoff.h').read_text()
        post=adapter.split('CepheusRpmhTailHandoff (',1)[1]
        for forbidden in ('gBS','Locate','FreePool','AllocatePool','Bind (','smc'):
            assert forbidden not in post,forbidden
        parser='STATIC\nBOOLEAN\nAdtNode'+text.split('STATIC\nBOOLEAN\nAdtNode',1)[1].split('STATIC\nBOOLEAN\nValidTrustCaches',1)[0]
        wrapper='''
int parse(const UINT8 *p,UINTN n) {
 UINTN o=0,r=MAX_UINTN,t=MAX_UINTN,s=MAX_UINTN,h=MAX_UINTN;UINT64 w=0;BOOLEAN k=0;
 return AdtNode(p,n,&o,0,3,&r,&t,&w,&k,&s,&h) && o==n && h!=MAX_UINTN && s==MAX_UINTN;
}
'''
        src=temp/'parser.c';src.write_text(PREAMBLE+'#include "cepheus_rpmh_tail_handoff.h"\n'+parser+wrapper)
        library=temp/'parser.so'
        subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-fsanitize=undefined',
            '-I',str(ROOT/'xnu'),'-I',str(driver),str(src),'-o',str(library)],check=True)
        parse=ctypes.CDLL(str(library)).parse;parse.argtypes=[ctypes.c_char_p,ctypes.c_size_t]
        good=tree();assert parse(good,len(good))
        assert not parse(None,len(good))
        for bad in (tree('elsewhere'),tree(size=1127),tree(size=1129),tree(nonzero=True),tree(duplicate=True),good+b'junk'):
            assert not parse(bad,len(bad))
        for size in range(len(good)):assert not parse(good[:size],size)
        receipt['steps'].append('actual ADT exact/zero/chosen/duplicate/truncation; prepare-before-map and copy-after-EBS PASS')
        artifact=build/'build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi'
        module_dir=artifact.parent/'DarwinBootPkg/Driver/DarwinBoot'
        makefile=module_dir/'GNUmakefile';mapfile=module_dir/'DEBUG/DarwinBoot.map'
        flags=subprocess.check_output(['make','-s','-f',str(makefile.resolve()),'--eval',
            'codex_flags:;@printf "%s\\n" "$(CC_FLAGS) $(INC)"','codex_flags'],cwd=module_dir,text=True)
        native_flags=shlex.split(flags)
        native=temp/'native';native.mkdir();source=native/'DarwinBoot.c';source.write_text(text)
        result=subprocess.run(['clang',*native_flags,'-fno-lto','--analyze',
            '-Xanalyzer','-analyzer-output=text','-I',str(driver),str(source),'-o',os.devnull],
            cwd=module_dir,capture_output=True,text=True,timeout=120)
        if result.returncode:print(result.stderr)
        result.check_returncode();warnings=re.findall(r'warning: ([^\n]+)',result.stderr)
        STAGE.stage(ROOT.parent/'unixnu/drivers/DarwinBoot',temp/'baseline')
        source.write_bytes((temp/'baseline/DarwinBootPkg/Driver/DarwinBoot.c').read_bytes())
        base=subprocess.run(['clang',*native_flags,'-fno-lto','--analyze',
            '-Xanalyzer','-analyzer-output=text','-I',str(driver),str(source),'-o',os.devnull],
            cwd=module_dir,capture_output=True,text=True,timeout=120)
        if base.returncode:print(base.stderr)
        base.check_returncode()
        baseline_warnings=re.findall(r'warning: ([^\n]+)',base.stderr)
        output.parent.mkdir(parents=True,exist_ok=True)
        output.with_name(output.stem+'-native.log').write_text(result.stderr)
        output.with_name(output.stem+'-baseline-native.log').write_text(base.stderr)
        unexpected=Counter(warnings)-Counter(baseline_warnings)
        if unexpected:
            print('Baseline:',baseline_warnings,'Staged:',warnings)
            print('Full diagnostics:',output.with_name(output.stem+'-native.log'))
        assert not unexpected, unexpected
        receipt['steps'].append({'native_analysis_warnings':warnings,'baseline_warnings':baseline_warnings,
            'no_additional_warning':True,'sha256':hashlib.sha256(result.stderr.encode()).hexdigest()})
        assembly=temp/'loader.s'
        subprocess.run(['clang',*native_flags,'-fno-lto','-S',str(built/'DarwinBoot.c'),'-o',str(assembly)],
            cwd=module_dir,check=True,timeout=120)
        asm=assembly.read_text()
        assert 'CepheusRpmhTailPrepare' in asm and 'CepheusRpmhTailHandoff' in asm
        receipt['steps'].append({'native_assembly_sha256':hashlib.sha256(asm.encode()).hexdigest()})
        receipt['build']={'source_matches_tested_stage':True,'bytes':artifact.stat().st_size,
            'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest(),
            'map_sha256':hashlib.sha256(mapfile.read_bytes()).hexdigest(),
            'makefile_sha256':hashlib.sha256(makefile.read_bytes()).hexdigest(),
            'native_flags_sha256':hashlib.sha256(flags.encode()).hexdigest()}
        for path in ('scripts/test-rpmh-tail-loader.py','scripts/stage-scm-pair-firmware.py','scripts/build-scm-pair-firmware.sh'):
            receipt['files'][path]=hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
    output.write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware-build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();main(args.firmware_build.resolve(),args.output.resolve())
