#!/usr/bin/env python3
"""Execute actual exit-tail collector and core allocator against RAM-only fixtures."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('tail_stage', ROOT/'scripts/stage-pool-lifetime-core.py')
STAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STAGE)

HOST_REGISTERS = r'''
static unsigned fake_pars,fake_fail_par,fake_masked,fake_revoke,fake_bad_par,fake_bad_el;
static unsigned fake_late_par,fake_late_kind;
static UINT64 fake_ticks,fake_frequency,fake_revoke_base;
BOOLEAN CoreCepheusTailRejectMutation(VOID);
static UINT64 CeTailRegister(UINTN which) {
 if(which==0)return fake_bad_el?8:4;
 if(which==1)return fake_ticks++;
 if(which==2)return fake_frequency;
 if(which==3)return 0;
 return 5;
}
static UINT64 CeTailPar(UINT64 p) {
 assert(fake_masked);fake_pars++;
 if(fake_pars==fake_late_par) {
  if(fake_late_kind==1)fake_ticks=0;
  if(fake_late_kind==2)fake_ticks+=fake_frequency;
  if(fake_late_kind==3)fake_frequency++;
  if(fake_late_kind==4)mCepheusPoolLifetime.Serial++;
  if(fake_late_kind==5)CoreCepheusTailRejectMutation();
 }
 if(fake_pars==fake_fail_par) {
  if(fake_revoke)assert(!mprotect((void *)(UINTN)fake_revoke_base,4096,PROT_NONE));
  return 1;
 }
 return fake_bad_par?UINT64_C(0x4400000000000800)|p:UINT64_C(0xff00000000000800)|p;
}
static UINT64 CeTailMask(void) {UINT64 old=fake_masked;fake_masked=1;return old;}
static void CeTailRestore(UINT64 old) {fake_masked=(unsigned)old;}
static EFI_STATUS TailGetAddress(void *protocol,const CHAR8 *name,UINT32 *output) {
 assert(protocol);assert(!fake_masked);*output=strcmp(name,"cx.lvl")==0?0x30000:0x30004;return 0;
}
'''


def main(build, output):
    if output.exists():
        raise ValueError('output already exists')
    receipt = {'status': 'PASS', 'phone_operations': False, 'gpu_execution': False, 'steps': [], 'files': {}}
    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        STAGE.stage(ROOT.parent/'uefi/MU_BASECORE', temp/'source', rpmh_tail=True)
        core = temp/'source'/STAGE.CORE
        built = build/'source'/STAGE.CORE
        for path in sorted(core.rglob('*')):
            if path.is_file():
                name = str(path.relative_to(core))
                assert path.read_bytes() == (built/name).read_bytes(), name
                receipt['files'][name] = hashlib.sha256(path.read_bytes()).hexdigest()
        maintext = (core/'DxeMain/DxeMain.c').read_text()
        begin = maintext.index('CoreExitBootServices (')
        exitfn = maintext[begin:maintext.index('\n}',begin)+2]
        assert exitfn.index('CoreNotifySignalList (&gEfiEventExitBootServicesGuid)') < exitfn.index('gBS = NULL')
        assert exitfn.index('gBS = NULL') < exitfn.index('gRuntime->AtRuntime = TRUE') < exitfn.index('CoreCepheusRpmhTail ();')
        assert exitfn.index('MemoryProtectionExitBootServicesCallback ();') < exitfn.index('CoreCepheusRpmhTail ();')
        (temp/'ExitUnderTest.c').write_text('#pragma clang diagnostic push\n#pragma clang diagnostic ignored "-Wunused-parameter"\nEFI_STATUS\nEFIAPI\n'+exitfn+'\n#pragma clang diagnostic pop\n')
        pool = (core/'Mem/Pool.c').read_text()
        for name in ('DxeMain.h', 'Imem.h', 'HeapGuard.h'):
            pool = pool.replace(f'#include "{name}"', '')
        (temp/'PoolUnderTest.c').write_text(pool)
        page = (core/'Mem/Page.c').read_text()
        function = 'EFI_STATUS\nCoreConvertPagesEx (\n'+page.split('EFI_STATUS\nCoreConvertPagesEx (\n', 1)[1].split('\n/**\n  Internal function.  Converts a memory range to the specified type.', 1)[0]
        function += '\nMEMORY_MAP *\nAllocateMemoryMapEntry (\n'+page.split('MEMORY_MAP *\nAllocateMemoryMapEntry (\n',1)[1].split('\n/**\n  Internal function.  Moves any memory descriptors',1)[0]
        (temp/'PageUnderTest.c').write_text(function)
        tail = (core/'Mem/CepheusRpmhTailCore.h').read_text()
        begin = tail.index('STATIC UINT64\nCeTailRegister (')
        end = tail.index('STATIC int\nCeTailBudget (')
        tail = tail[:begin]+HOST_REGISTERS+'\n'+tail[end:]
        old = 'GET_ADDRESS GetAddress = (GET_ADDRESS)(UINTN)(Copied[2] + 0x1444);'
        assert tail.count(old) == 1
        tail = tail.replace(old, 'GET_ADDRESS GetAddress = TailGetAddress;')
        assert '__asm__' not in tail
        (temp/'CepheusRpmhTailCore.h').write_text(tail)
        for name in ('CepheusRpmhTailMap.h', 'CepheusRpmhSnapshotIdentity.h'):
            (temp/name).write_bytes((core/'Mem'/name).read_bytes())
        fixture = (ROOT/'firmware/test_pool_lifetime_core.c').read_text()
        fixture = fixture.replace('typedef uint8_t BOOLEAN;', 'typedef uint8_t BOOLEAN;\ntypedef uint8_t UINT8;')
        fixture = fixture.replace('#define EFI_NOT_FOUND', '#define EFI_MEMORY_WB 8\n#define EFI_MEMORY_RP 0x2000\n#define EFI_COMPROMISED_DATA (UINT64_C(1)<<63|33)\n#define EFI_NOT_READY (UINT64_C(1)<<63|6)\n#define CopyMem memcpy\n#define ZeroMem(p,n) memset(p,0,n)\n#define EFI_NOT_FOUND', 1)
        extra = '''typedef struct {UINT32 Type,Pad;UINT64 PhysicalStart,VirtualStart,NumberOfPages,Attribute;} EFI_MEMORY_DESCRIPTOR;
static struct {BOOLEAN AtRuntime;} fake_runtime;
static __typeof__(fake_runtime) *gRuntime=&fake_runtime;
static void *gBS;
'''
        fixture = fixture.replace('#include "PoolUnderTest.c"', extra+'\n#include "PoolUnderTest.c"', 1)
        fixture = fixture.replace('#include "PageUnderTest.c"', '#include "CepheusRpmhTailMap.h"\n#include "PageUnderTest.c"', 1)
        fixture = fixture.replace('static UINTN mMapDepth;', 'static UINTN mMapDepth;\nstatic LIST_ENTRY mFreeMemoryMapEntryList;')
        fixture = fixture.replace('p==&mCepheusPoolQuery', '(p==&mCepheusPoolQuery||p==&mCeTailProtocol)', 1)
        fixture = fixture.replace('assert(installs==1&&mCepheusPoolQuery.Version==1)', 'assert(installs==2&&mCepheusPoolQuery.Version==1)', 1)
        fixture = fixture.replace('int main(void)', '#include "test_rpmh_tail_core.c"\n#include "test_rpmh_tail_exit.c"\nint main(void)', 1)
        fixture = fixture.replace('actual_page_conversion();', 'actual_page_conversion();tail_tests();tail_exit_tests();', 1)
        src = temp/'fixture.c'
        src.write_text(fixture)
        for optimize in ('-O0', '-O2'):
            exe = temp/('run'+optimize)
            command = ['clang', '-std=gnu11', '-D_DEFAULT_SOURCE', optimize, '-Wall', '-Wextra', '-Werror',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(temp),
                       '-I', str(ROOT/'firmware'), str(src), '-o', str(exe)]
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                print(result.stderr);result.check_returncode()
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=300)
            print(result.stdout, end='')
            if result.returncode:
                print(result.stderr);result.check_returncode()
            receipt['steps'].append({'optimization': optimize, 'output': result.stdout.strip()})
        artifact = build/'build/PoolLifetimeCore/RELEASE_CLANGPDB/AARCH64/DxeCore.efi'
        assert (ROOT/'firmware/PoolLifetimeCore.dsc').read_bytes() == (build/'PoolLifetimeCore.dsc').read_bytes()
        manifest=json.loads((build/'source/pool-lifetime-source.json').read_text())
        assert manifest['rpmh_exit_tail_diagnostic'] and not manifest['bookkeeping_only']
        for name,digest in manifest['files'].items():
            assert hashlib.sha256((build/'source'/name).read_bytes()).hexdigest()==digest,name
        buildlog=(build/'build.log').read_text()
        assert '- Done -' in buildlog and '- Failed -' not in buildlog
        module=artifact.parent/'MdeModulePkg/Core/Dxe/DxeMain'
        response=module/'OUTPUT/cc_resp.txt'
        mapfile=module/'DEBUG/DxeCore.map'
        symbols=mapfile.read_text()
        # LTO inlines the exit/install/attempt wrappers into their real callers.
        for name in ('CoreCepheusTailMap','CoreCepheusTailProbe','CeTailBind','CeTailCopy','mCeTailRecord','mCeTailProtocol'):
            assert name in symbols,name
        diagnostics={}
        native=temp/'native';native.mkdir()
        for name in ('Pool','Page'):
            original=subprocess.check_output(['git','-C',str(ROOT.parent/'uefi/MU_BASECORE'),
                'show',STAGE.BASE+':'+str(STAGE.CORE/'Mem'/f'{name}.c')]).decode()
            for variant,body in (('baseline',original),('staged',(core/'Mem'/f'{name}.c').read_text())):
                source=native/f'{variant}-{name}.c';source.write_text(body)
                result=subprocess.run(['clang','@'+str(response.resolve()),'-fno-lto','--analyze',
                    '-Xanalyzer','-analyzer-output=text','-I',str(core/'Mem'),str(source),'-o',os.devnull],
                    cwd=module,capture_output=True,text=True,timeout=120)
                if result.returncode: print(result.stderr)
                result.check_returncode();assert 'error:' not in result.stderr
                warnings=re.findall(r'warning: ([^\n]+)',result.stderr)
                diagnostics[f'{variant}-{name}']=warnings
                logfile=output.with_name(output.stem+f'-{variant}-{name}.log')
                assert not logfile.exists();logfile.parent.mkdir(parents=True,exist_ok=True);logfile.write_text(result.stderr)
                receipt['steps'].append({'native_analysis':f'{variant}-{name}','warnings':warnings,
                    'diagnostics_sha256':hashlib.sha256(result.stderr.encode()).hexdigest()})
            assert diagnostics[f'baseline-{name}']==diagnostics[f'staged-{name}']
        assert not diagnostics['staged-Pool'] and len(diagnostics['staged-Page'])==2
        assembly=temp/'Pool.s'
        subprocess.run(['clang','@'+str(response.resolve()),'-fno-lto','-S',str(core/'Mem/Pool.c'),'-o',str(assembly)],
            cwd=module,check=True,timeout=120)
        asm=assembly.read_text()
        for pattern in (r'at\s+s1e1r',r'mrs\s+\w+,\s*PAR_EL1',r'msr\s+DAIFSet,\s*#15',r'msr\s+DAIF,',r'dsb\s+sy',r'isb'):
            assert re.search(pattern,asm,re.IGNORECASE),pattern
        receipt['steps'].append({'native_assembly':'AT/ISB/PAR/DAIF mask+restore present',
            'sha256':hashlib.sha256(asm.encode()).hexdigest()})
        assembly=temp/'DxeMain.s'
        subprocess.run(['clang','@'+str(response.resolve()),'-fno-lto','-S',str(core/'DxeMain/DxeMain.c'),'-o',str(assembly)],
            cwd=module,check=True,timeout=120)
        asm=assembly.read_text()
        for name in ('CoreCepheusTailAttempt','CoreCepheusRpmhTail','CoreCepheusPoolQueryInstall'):
            assert re.search(r'bl\s+'+name,asm),name
        receipt['steps'].append({'native_assembly':'real DXE main calls attempt, installed protocol and exit-tail',
            'sha256':hashlib.sha256(asm.encode()).hexdigest()})
        for name in ('firmware/test_pool_lifetime_core.c','firmware/test_rpmh_tail_core.c',
                     'firmware/test_rpmh_tail_exit.c','scripts/test-rpmh-tail-core.py',
                     'scripts/stage-pool-lifetime-core.py','scripts/build-pool-lifetime-core.sh',
                     'scripts/generate-rpmh-snapshot-identity.py','firmware/PoolLifetimeCore.dsc'):
            receipt['files'][name]=hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
        receipt['artifact'] = {'bytes': artifact.stat().st_size, 'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest()}
        receipt['build']={'source_matches_tested_stage':True,'build_log_sha256':hashlib.sha256(buildlog.encode()).hexdigest(),
            'manifest_sha256':hashlib.sha256((build/'source/pool-lifetime-source.json').read_bytes()).hexdigest(),
            'map_sha256':hashlib.sha256(mapfile.read_bytes()).hexdigest(),
            'response_sha256':hashlib.sha256(response.read_bytes()).hexdigest()}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware-build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    main(args.firmware_build.resolve(), args.output.resolve())
