#!/usr/bin/env python3
"""Local ARC/BCM sender acceptance. Never accesses a phone or sends votes."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--android',type=Path)
    parser.add_argument('--patches',type=Path,default=ROOT.parent/'inputs/ios-sweet-ordered-touch/xnu/patches')
    args=parser.parse_args()
    pinned=args.patches.resolve()
    if not pinned.is_dir():parser.error('supply the pinned ios-sweet patch directory')
    temporary=tempfile.TemporaryDirectory() if not args.output else None
    out=args.output.resolve() if args.output else Path(temporary.name)
    if args.output:out.mkdir(parents=True,exist_ok=False)
    names=('xnu/cepheus_bcm_active.h','xnu/cepheus_bcm_active_private.h',
           'xnu/cepheus_rpmh_active_private.h','xnu/cepheus_arc_active_private.h',
           'xnu/cepheus_arc_active.h','xnu/cepheus_bcm.h','xnu/cepheus_arc.h',
           'xnu/cepheus_rpmh_scope.h','xnu/cepheus_rpmh_scope_private.h',
           'xnu/test_bcm_active.c','xnu/test_arc_active.c','scripts/test-bcm-active.py',
           'scripts/test-arc-active-native.py','scripts/test-gpu-stack.sh',
           'scripts/test-gpu-sanitizers.sh','scripts/verify-gpu-kernel.py',
           'scripts/test-gpu-integration.py','scripts/test-rpmh-tail-hook.py')
    receipt={'time_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
             'source_sha256':{n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in names},
             'phone_operations':False,'full_xnu_build':False,'gpu_execution':False,
             'actual_votes_sent':False,'steps':[],'result':'RUNNING'}
    if args.android:
        android=args.android.resolve()
        pin='36f369fdb77319a2984a48b89f025ac3e5f97386'
        assert subprocess.check_output(['git','-C',str(android),'rev-parse','HEAD'],text=True).strip()==pin
        paths=('drivers/mailbox/qcom-rpmh-mailbox.c',
               'drivers/soc/qcom/msm_bus/msm_bus_fabric_rpmh.c',
               'drivers/soc/qcom/msm_bus/msm_bus_arb_rpmh.c')
        subprocess.run(['git','-C',str(android),'diff','--exit-code','HEAD','--',*paths],check=True)
        fabric=(android/paths[1]).read_text()
        generation=fabric.split('static int tcs_cmd_gen(',1)[1].split('static int tcs_cmd_list_gen(',1)[0]
        assert 'cmd->complete = commit;' in generation and 'BCM_TCS_CMD(commit, valid, vec_a, vec_b)' in generation
        mailbox=(android/paths[0]).read_text()
        programming=mailbox.split('static void __tcs_buffer_write(',1)[1].split('rsc_drv_is_idle:',1)[0]
        assert 'cmd_complete |= cmd->complete << (n + i);' in programming
        assert 'msgid |= (cmd->complete) ? CMD_MSGID_RESP_REQ : 0;' in programming
        assert '(resp->msg->is_complete || cmd->complete)' in mailbox
        receipt['android_reference']={'commit':pin,'sha256':{
            p:hashlib.sha256((android/p).read_bytes()).hexdigest() for p in paths}}

    def run(label,command,env=None):
        log=out/(label+'.log')
        with log.open('w') as stream:
            code=subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=120).returncode
        body=log.read_text()
        if 'warning:' in body or 'error:' in body:code=code or 1
        receipt['steps'].append({'label':label,'command':command,'exit_code':code,
                                 'log':log.name,'sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
        receipt['result']='FAIL' if code else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print(label,'FAIL' if code else 'PASS',flush=True)
        if code:raise SystemExit(body)

    common=['-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'xnu')]
    env=os.environ.copy()
    env['ASAN_OPTIONS']=('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1'
    for variant,compiler,flags in (
        ('host',os.environ.get('CC','cc'),['-O2','-fsanitize=undefined']),
        ('sanitized','clang',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
        for name in ('arc_active','bcm_active','bcm_crypto_active'):
            exe=out/(variant+'-'+name)
            source='bcm_active' if name=='bcm_crypto_active' else name
            route_flags=['-DTEST_CRYPTO_ROUTE=1'] if name=='bcm_crypto_active' else []
            run(variant+'-'+name+'-compile',[compiler,*common,*flags,*route_flags,str(ROOT/'xnu'/('test_'+source+'.c')),'-o',str(exe)])
            run(variant+'-'+name+'-run',[str(exe)],env)
    for name in ('arc_active','bcm_active','bcm_crypto_active'):
        source='bcm_active' if name=='bcm_crypto_active' else name
        route_flags=['-DTEST_CRYPTO_ROUTE=1'] if name=='bcm_crypto_active' else []
        run(name+'-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
                              *route_flags,str(ROOT/'xnu'/('test_'+source+'.c')),'-o',os.devnull])
    run('native',['python3',str(ROOT/'scripts/test-arc-active-native.py'),str(pinned)])
    run('integration',['python3',str(ROOT/'scripts/test-gpu-arc.py'),str(pinned)])
    run('boot-no-call',['python3',str(ROOT/'scripts/test-gpu-integration.py')])
    module_spec=importlib.util.spec_from_file_location('verification',ROOT/'scripts/verify-gpu-kernel.py')
    verification=importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(verification)
    patches=out/'patches';patches.mkdir()
    for name in verification.HEADERS:shutil.copyfile(ROOT/'xnu'/name,patches/name)
    for name in ('cepheus_arc_probe.h','0086-stage-cepheus-gpu-arc.sh'):
        shutil.copyfile(ROOT/'xnu'/name,patches/name)
    tree=out/'staged';(tree/'bsd/conf').mkdir(parents=True);(tree/'bsd/dev').mkdir()
    (tree/'bsd/conf/files').write_text('base\n')
    subprocess.run(['bash',str(patches/'0086-stage-cepheus-gpu-arc.sh')],cwd=tree,check=True)
    for name in ('cepheus_bcm_active.h','cepheus_bcm_active_private.h','cepheus_rpmh_active_private.h',
                 'cepheus_arc_active.h','cepheus_arc_active_private.h','cepheus_arc.h','cepheus_bcm.h',
                 'cepheus_rpmh_scope.h','cepheus_rpmh_scope_private.h'):
        assert (tree/'bsd/dev'/name).read_bytes()==(ROOT/'xnu'/name).read_bytes()
    run('staged-native',['python3',str(ROOT/'scripts/test-arc-active-native.py'),str(pinned),'--headers',str(tree/'bsd/dev')])
    receipt['result']='PASS'
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('ARC/BCM local sender acceptance PASS; real owners/votes/GPU execution not validated')
    if args.output:print(out/'receipt.json')
    if temporary:temporary.cleanup()


if __name__=='__main__':main()
