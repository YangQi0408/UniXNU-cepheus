#!/usr/bin/env python3
"""Local parent hold/counter acceptance; no phone measurement or full XNU build."""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--android', type=Path)
    parser.add_argument('--gmu', type=Path)
    parser.add_argument('--sqe', type=Path)
    args = parser.parse_args()
    if bool(args.gmu)!=bool(args.sqe):
        parser.error('--gmu and --sqe must be supplied together')
    temporary = tempfile.TemporaryDirectory() if not args.output else None
    out = args.output.resolve() if args.output else Path(temporary.name)
    if args.output:
        out.mkdir(parents=True, exist_ok=False)
    receipt = {'time_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
               'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               'phone_operations': False, 'full_xnu_build': False, 'frequency_measured': False,
               'source_sha256': {}, 'steps': [], 'result': 'RUNNING'}
    names = ('xnu/cepheus_gpll0.h', 'xnu/cepheus_gpll0_private.h', 'xnu/test_gpll0.c',
             'xnu/cepheus_clock_measure.h', 'xnu/cepheus_clock_measure_private.h', 'xnu/test_clock_measure.c',
             'xnu/cepheus_gpu_power_native.h','xnu/cepheus_gpu_power_native_private.h',
             'xnu/cepheus_gpu_power_mmio.h','xnu/test_gpu_power_native.c','xnu/test_gpu_power_mmio.c',
             'xnu/cepheus_gpu_tsens.h','xnu/cepheus_gpu_tsens_native.h','xnu/cepheus_gpu_tsens_native_private.h',
             'xnu/test_gpu_tsens.c','xnu/test_gpu_tsens_native.c',
             'xnu/cepheus_gmu_start_native.h','xnu/cepheus_gmu_start_native_private.h',
             'xnu/cepheus_gmu_mmio.h','xnu/test_gmu_start_native.c','xnu/test_gmu_mmio.c',
             'xnu/cepheus_gpu_pdc_native.h','xnu/cepheus_gpu_pdc_native_private.h',
             'xnu/cepheus_pdc_mmio.h','xnu/test_gpu_pdc_native.c','xnu/test_pdc_mmio.c',
             'xnu/cepheus_smmu_native.h','xnu/cepheus_smmu_native_private.h',
             'xnu/cepheus_smmu_mmio.h','xnu/test_smmu_native.c','xnu/test_smmu_mmio.c',
             'xnu/cepheus_gpu_memory.cpp','xnu/cepheus_gpu_memory_private.h','tests/test_smmu_memory_pin.cpp',
             'tests/test_smmu_native_pool.cpp','scripts/test-gpu-memory-host.sh',
             'xnu/cepheus_gmu_upload_native.h','xnu/cepheus_gmu_upload_native_private.h',
             'xnu/cepheus_gmu_upload.h','xnu/cepheus_gmu_upload_mmio.h','xnu/test_gmu_upload_native.c',
             'xnu/test_gmu_upload.c','xnu/test_gmu_upload_mmio.c',
             'tests/test_upload_memory_pin.cpp','tests/test_upload_native_pool.cpp',
             'xnu/cepheus_gpu_compile.c', 'xnu/cepheus_gpu_compile.h', 'scripts/test-gpll0.py')
    names += ('tests/gpll0-scope-fixture.h','xnu/cepheus_rpmh_scope.h','xnu/cepheus_rpmh_scope_private.h')
    names += ('xnu/cepheus_qmp_native.h','xnu/cepheus_qmp_native_private.h','xnu/test_qmp_native.c',
              'xnu/cepheus_qmp_mmio.h','xnu/cepheus_qmp_connect.h','xnu/cepheus_qmp_acd.h',
              'xnu/test_qmp_mmio.c','xnu/test_qmp_connect.c')
    names += ('xnu/cepheus_gmu_oob.h','xnu/cepheus_gmu_oob_native.h',
              'xnu/cepheus_gmu_oob_native_private.h','xnu/test_gmu_oob.c','xnu/test_gmu_oob_native.c')
    names += ('xnu/cepheus_hfi.h','xnu/cepheus_hfi_session.h','xnu/cepheus_gmu_hfi_link.c',
              'xnu/cepheus_gmu_hfi_link.h','xnu/test_hfi_access.c','tests/test_hfi_link_access.c',
              'xnu/cepheus_gmu_workspace.h','xnu/cepheus_gpu_memory.h','xnu/cepheus_gpu_retire.h')
    names += ('xnu/cepheus_gmu_hfi_native.h','xnu/cepheus_gmu_hfi_native_private.h',
              'xnu/test_gmu_hfi_native.c','tests/test_hfi_memory_pin.cpp',
              'xnu/test_gmu_hfi_native_pool.c','tests/test_hfi_native_pool.cpp',
              'scripts/verify-gpu-kernel.py','scripts/build-kernel-patches.sh',
              'xnu/0087-stage-cepheus-gpu-memory.sh','.github/workflows/touch-kernel.yml')
    names += ('xnu/cepheus_gpu_config_native.h','xnu/cepheus_gpu_config_native_private.h',
              'xnu/test_gpu_config_native.c','xnu/test_hfi_session.c','scripts/test-hfi-native-pool.sh')
    names += ('xnu/cepheus_gx_core.h','xnu/cepheus_gx_core_native.h',
              'xnu/cepheus_gx_core_native_private.h','xnu/test_gx_core_native.c',
              'xnu/cepheus_cp_protection.h','xnu/cepheus_cp_protect.h',
              'xnu/cepheus_gx_hwcg.h','xnu/cepheus_gx_gbif.h','xnu/cepheus_gx_perfcounter.h',
              'scripts/audit-gx-setup-source.py')
    names += ('xnu/cepheus_cp_workspace.c','xnu/cepheus_cp_workspace.h','xnu/cepheus_cp_memory_private.h',
              'xnu/cepheus_gx_smmu_plan.h','xnu/cepheus_smmu_geometry.h','xnu/cepheus_sqe.h',
              'xnu/test_gx_smmu_plan.c','scripts/test-gpu-stack.sh',
              'tests/test_cp_workspace.cpp','tests/test_cp_workspace_restore.c','scripts/test-cp-memory-pool.sh')
    names += ('xnu/cepheus_cp_memory_api.h','xnu/cepheus_smmu_install.h','xnu/cepheus_gx_smmu_install.h',
              'xnu/cepheus_gx_smmu_native.h','xnu/cepheus_gx_smmu_native_private.h',
              'xnu/test_gx_smmu_native.c','tests/test_gx_smmu_native_pool.cpp',
              'scripts/test-gx-smmu-native-pool.sh')
    names += ('xnu/cepheus_cp_native.h','xnu/cepheus_cp_native_private.h','xnu/test_cp_native.c',
              'tests/test_cp_build_pool.cpp','scripts/test-cp-build-pool.sh')
    names += ('xnu/cepheus_cp_ring_native.h','xnu/cepheus_cp_ring_native_private.h','xnu/test_cp_ring_native.c',
              'scripts/test-cp-ring-native-pool.sh')
    names += ('xnu/cepheus_cp_execution_admission.h','xnu/cepheus_cp_execution_admission_private.h',
              'tests/test_cp_execution_admission.c','tests/test_cp_execution_admission_native.c')
    names += ('xnu/cepheus_owner_evidence.h','xnu/test_owner_evidence.c',
              'xnu/cepheus_zap_resource_evidence.h','xnu/test_zap_resource_evidence.c')
    receipt['source_sha256'] = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}
    if args.android:
        android = args.android.resolve()
        pin = '36f369fdb77319a2984a48b89f025ac3e5f97386'
        head = subprocess.check_output(['git', '-C', str(android), 'rev-parse', 'HEAD'], text=True).strip()
        if head != pin:
            raise SystemExit('Wrong SM8150 reference commit')
        files = ('gcc-sm8150.c', 'gpucc-sm8150.c', 'clk-alpha-pll.c', 'clk-branch.c', 'clk-regmap.c',
                 'clk-debug.c', 'debugcc-sm8150.c')
        paths = ['drivers/clk/qcom/'+name for name in files]
        subprocess.run(['git', '-C', str(android), 'diff', '--exit-code', 'HEAD', '--', *paths], check=True)
        texts = {name: (android/'drivers/clk/qcom'/name).read_text() for name in files}
        receipt['android_reference'] = {'commit': pin, 'sha256': {
            name: hashlib.sha256((android/'drivers/clk/qcom'/name).read_bytes()).hexdigest() for name in files}}
        def require(name, pattern):
            if not re.search(pattern, texts[name], re.S):
                raise SystemExit('Reference rule changed: '+name+' '+pattern)
        require('gcc-sm8150.c', r'static struct clk_alpha_pll gpll0 = \{.*?\.offset = 0x0,.*?'
                r'\.type = TRION_PLL,.*?\.enable_reg = 0x52000,.*?\.enable_mask = BIT\(0\),')
        require('gcc-sm8150.c', r'static struct clk_gate2 gcc_gpu_gpll0_div_clk_src = \{.*?'
                r'\.udelay = 500,.*?\.enable_reg = 0x52004,.*?\.enable_mask = BIT\(16\),.*?'
                r'"gpll0_out_even".*?\.ops = &clk_gate2_ops,')
        require('gcc-sm8150.c', r'post_div_table_trion_even\[\] = \{\s*'
                r'\{ 0x0, 1 \},\s*\{ 0x1, 2 \},\s*\{ 0x3, 4 \},\s*\{ 0x7, 8 \}')
        require('clk-alpha-pll.c', r'#define TRION_PLL_USER_CTL\s+0xc')
        require('clk-alpha-pll.c', r'#define TRION_PLL_STATUS\s+0x30')
        require('clk-alpha-pll.c', r'#define TRION_PLL_ALPHA_VAL\s+0x40')
        require('clk-alpha-pll.c', r'#define TRION_PCAL_DONE\s+BIT\(26\)')
        require('clk-alpha-pll.c', r'if \(val & PLL_VOTE_FSM_ENA\) \{\s*'
                r'ret = clk_enable_regmap\(hw\);\s*if \(ret\)\s*return ret;\s*'
                r'return wait_for_pll_enable_active\(pll\);')
        require('clk-alpha-pll.c', r'alpha_bw = ALPHA_REG_16BITWIDTH;\s*'
                r'return \(prate \* l\) \+ \(\(prate \* a\) >> alpha_bw\);')
        require('clk-branch.c', r'clk_gate_toggle.*?ret = clk_enable_regmap\(hw\);.*?'
                r'if \(gt->udelay\)\s*udelay\(gt->udelay\);')
        require('clk-regmap.c', r'clk_enable_regmap.*?regmap_update_bits\(.*?'
                r'rclk->enable_reg,.*?rclk->enable_mask,.*?val\)')
        require('gpucc-sm8150.c', r'P_GPLL0_OUT_MAIN_DIV, 6')
        require('gpucc-sm8150.c', r'F\(200000000, P_GPLL0_OUT_MAIN_DIV, 1\.5, 0, 0\)')
        require('debugcc-sm8150.c', r'\.ctl_reg = 0x62038,\s*\.status_reg = 0x6203C,\s*'
                r'\.xo_div4_cbcr = 0x43008,')
        require('debugcc-sm8150.c', r'"gcc_gpu_gpll0_div_clk_src", 0x167, 1, GCC,\s*'
                r'0x167, 0x3FF, 0, 0xF, 0, 1, 0x62000, 0x62004, 0x62008')
        require('clk-debug.c', r'#define XO_DIV4_CNT_DONE\s+BIT\(25\)')
        require('clk-debug.c', r'#define CNT_EN\s+BIT\(20\)')
        require('clk-debug.c', r'#define MEASURE_CNT\s+GENMASK\(24, 0\)')
        require('clk-debug.c', r'raw_count_full == raw_count_short')
        require('clk-debug.c', r'raw_count_full = \(\(raw_count_full \* 10\) \+ 15\) \* TCXO_DIV_4_HZ;\s*'
                r'do_div\(raw_count_full, \(\(SAMPLE_TICKS_14_MS \* 10\) \+ 35\)\);')
        qmp_paths=('drivers/mailbox/msm_qmp.c','drivers/gpu/msm/kgsl_gmu.c',
                   'arch/arm64/boot/dts/qcom/sm8150.dtsi')
        for path in qmp_paths:
            recorded=subprocess.check_output(['git','-C',str(android),'show',pin+':'+path])
            if recorded!=(android/path).read_bytes():raise SystemExit('Modified QMP reference: '+path)
            receipt['android_reference']['sha256'][path]=hashlib.sha256(recorded).hexdigest()
        qmp=(android/qmp_paths[0]).read_text();gmu=(android/qmp_paths[1]).read_text()
        dts=(android/qmp_paths[2]).read_text()
        patterns=((qmp,r'#define QMP_MAGIC\s+0x4d41494c'),
                  (qmp,r'#define QMP_VERSION\s+0x1'),
                  (qmp,r'#define QMP_TOUT_MS\s+5000'),
                  (qmp,r'#define QMP_TX_TOUT_MS\s+1000'),
                  (qmp,r'memcpy32_toio\(addr \+ sizeof\(pkt->size\), pkt->data, pkt->size\);\s*'
                       r'iowrite32\(pkt->size, addr\);'),
                  (qmp,r'writel_relaxed\(mdev->irq_mask, mdev->tx_irq_reg\);'),
                  (gmu,r'"\{class: gpu, res: acd, value: %d\}", state'),
                  (dts,r'qmp_aop:.*?reg = <0xc300000 0x1000>, <0x17c0000C 0x4>;.*?'
                       r'qcom,irq-mask = <0x1>;.*?mbox-desc-offset = <0x0>;'))
        for text,pattern in patterns:
            if not re.search(pattern,text,re.S):raise SystemExit('Changed QMP source rule: '+pattern)
        oob_paths=('drivers/gpu/msm/adreno_a6xx_gmu.c','drivers/gpu/msm/a6xx_reg.h',
                   'drivers/gpu/msm/kgsl_gmu_core.h')
        oob_text={}
        for path in oob_paths:
            recorded=subprocess.check_output(['git','-C',str(android),'show',pin+':'+path])
            if recorded!=(android/path).read_bytes():raise SystemExit('Modified OOB reference: '+path)
            receipt['android_reference']['sha256'][path]=hashlib.sha256(recorded).hexdigest()
            oob_text[path]=recorded.decode()
        rules=((oob_text[oob_paths[0]],r'#define GPU_START_TIMEOUT\s+100'),
               (oob_text[oob_paths[0]],r'set = BIT\(30 - req \* 2\);\s*check = BIT\(31 - req\);'),
               (oob_text[oob_paths[0]],r'set = BIT\(req \+ 16\);\s*check = BIT\(req \+ 16\);'),
               (oob_text[oob_paths[0]],r'clear = BIT\(31 - req \* 2\);'),
               (oob_text[oob_paths[0]],r'clear = BIT\(req \+ 24\);'),
               (oob_text[oob_paths[0]],r'A6XX_GMU_GMU2HOST_INTR_INFO,\s*check,\s*GPU_START_TIMEOUT,\s*check'),
               (oob_text[oob_paths[1]],r'#define A6XX_GMU_HOST2GMU_INTR_SET\s+0x1F994'),
               (oob_text[oob_paths[1]],r'#define A6XX_GMU_GMU2HOST_INTR_INFO\s+0x1F992'),
               (oob_text[oob_paths[1]],r'#define A6XX_GMU_GMU2HOST_INTR_CLR\s+0x1F991'),
               (oob_text[oob_paths[1]],r'#define A6XX_GMU_HOST2GMU_INTR_RAW_INFO\s+0x1F996'),
               (oob_text[oob_paths[2]],r'oob_gpu = 0,'))
        for text,pattern in rules:
            if not re.search(pattern,text,re.S):raise SystemExit('Changed OOB source rule: '+pattern)

    def run(label, command, env=None, timeout=120):
        log = out/(label+'.log')
        with log.open('w') as stream:
            try:
                code = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                      env=env, timeout=timeout).returncode
            except subprocess.TimeoutExpired:
                code = 124
            except KeyboardInterrupt:
                code = 130
        body = log.read_text()
        if 'warning:' in body or 'error:' in body or 'runtime error:' in body:
            code = code or 1
        receipt['steps'].append({'label': label, 'command': command, 'exit_code': code,
                                 'log': log.name, 'sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
        receipt['result'] = 'FAIL' if code else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(label, 'FAIL' if code else 'PASS', flush=True)
        if code:
            raise SystemExit(body[-6000:])

    common = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'xnu')]
    if args.android:
        run('gx-setup-vendor-source',['python3','scripts/audit-gx-setup-source.py',
            '--kernel',str(args.android),'--output',str(out/'gx-setup-vendor-source.json')])
    for name, compiler, flags in (
        ('host', os.environ.get('CC', 'cc'), ['-O2']),
        ('sanitized', 'clang', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
    ):
        exe = out/name
        run(name+'-compile', [compiler, *common, *flags, str(ROOT/'xnu/test_gpll0.c'), '-o', str(exe)])
        env = os.environ.copy()
        # Apple ASan has no LeakSanitizer; keep address/UB checks on both hosts.
        env['ASAN_OPTIONS'] = ('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1'
        env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        run(name+'-run', [str(exe)], env)
        exe = out/(name+'-measure')
        run(name+'-measure-compile', [compiler, *common, *flags, str(ROOT/'xnu/test_clock_measure.c'), '-o', str(exe)])
        run(name+'-measure-run', [str(exe)], env)
        exe = out/(name+'-power')
        run(name+'-power-compile', [compiler, *common, *flags, str(ROOT/'xnu/test_gpu_power_native.c'), '-o', str(exe)])
        run(name+'-power-run', [str(exe)], env)
        exe = out/(name+'-power-mmio')
        run(name+'-power-mmio-compile', [compiler, *common, *flags, str(ROOT/'xnu/test_gpu_power_mmio.c'), '-o', str(exe)])
        run(name+'-power-mmio-run', [str(exe)], env)
        for sensor in ('cp_ring_native','cp_native','gx_smmu_native','gx_core_native','gpu_config_native','hfi_session','hfi_access','gmu_oob','gmu_oob_native','qmp_mmio','qmp_connect','qmp_native','gpu_tsens','gpu_tsens_native','gmu_start_native','gmu_mmio','gpu_pdc_native','pdc_mmio','smmu_native','smmu_mmio','gmu_upload_native','gmu_upload','gmu_upload_mmio'):
            exe=out/(name+'-'+sensor)
            run(name+'-'+sensor+'-compile', [compiler,*common,*flags,str(ROOT/'xnu'/('test_'+sensor+'.c')),'-o',str(exe)])
            run(name+'-'+sensor+'-run',[str(exe)],env)
        exe=out/(name+'-cp-execution-admission')
        run(name+'-cp-execution-admission-compile',[compiler,*common,*flags,
            str(ROOT/'tests/test_cp_execution_admission.c'),'-o',str(exe)])
        run(name+'-cp-execution-admission-run',[str(exe)],env)
        exe=out/(name+'-cp-execution-admission-native')
        run(name+'-cp-execution-admission-native-compile',[compiler,*common,*flags,
            str(ROOT/'tests/test_cp_execution_admission_native.c'),'-o',str(exe)])
        run(name+'-cp-execution-admission-native-run',[str(exe)],env)
        exe=out/(name+'-owner-evidence')
        run(name+'-owner-evidence-compile',[compiler,*common,*flags,
            str(ROOT/'xnu/test_owner_evidence.c'),'-o',str(exe)])
        run(name+'-owner-evidence-run',[str(exe)],env)
        exe=out/(name+'-zap-resource-evidence')
        run(name+'-zap-resource-evidence-compile',[compiler,*common,*flags,
            str(ROOT/'xnu/test_zap_resource_evidence.c'),'-o',str(exe)])
        run(name+'-zap-resource-evidence-run',[str(exe)],env)
        exe=out/(name+'-hfi-link-access')
        run(name+'-hfi-link-access-compile',[compiler,*common,*flags,
            str(ROOT/'tests/test_hfi_link_access.c'),str(ROOT/'xnu/cepheus_gmu_hfi_link.c'),'-o',str(exe)])
        run(name+'-hfi-link-access-run',[str(exe)],env)
        exe=out/(name+'-hfi-native')
        run(name+'-hfi-native-compile',[compiler,*common,*flags,
            str(ROOT/'xnu/test_gmu_hfi_native.c'),str(ROOT/'xnu/cepheus_gmu_hfi_link.c'),'-o',str(exe)])
        run(name+'-hfi-native-run',[str(exe)],env)
    # Compile the actual native branch of the production translation unit.
    source = (ROOT/'xnu/cepheus_gpu_compile.c').read_text()
    native = source.split('static LCK_GRP_DECLARE', 1)[1].split('\n#endif', 1)[0]
    declarations = '''
#define KERNEL 1
#include "cepheus_gpu_compile.h"
#define LCK_GRP_DECLARE(n,s) int n __attribute__((unused))
#define LCK_MTX_DECLARE(n,g) int n __attribute__((unused))
extern void lck_mtx_lock(int *);
extern void lck_mtx_unlock(int *);
extern int ml_get_interrupts_enabled(void);
extern int ml_at_interrupt_context(void);
extern int get_preemption_level(void);
extern void disable_preemption(void);
extern void enable_preemption(void);
extern int printf(const char *, ...);
extern void delay(unsigned);
extern uintptr_t ml_io_map(uint64_t,size_t);
extern uint64_t mach_absolute_time(void);
extern void absolutetime_to_nanoseconds(uint64_t,uint64_t *);
'''
    file = out/'native.c'
    file.write_text(declarations+'static LCK_GRP_DECLARE'+native)
    command = ['clang', '--target=aarch64-none-elf', '-O2', '-ffreestanding', *common]
    run('native-compile', [*command, '-c', str(file), '-o', str(out/'native.o')])
    run('native-assembly', [*command, '-S', str(file), '-o', str(out/'native.s')])
    run('hfi-link-native-compile', [*command, '-fstack-usage', '-c',
        str(ROOT/'xnu/cepheus_gmu_hfi_link.c'), '-o', str(out/'hfi-link-native.o')])
    run('hfi-link-native-assembly', [*command, '-S',
        str(ROOT/'xnu/cepheus_gmu_hfi_link.c'), '-o', str(out/'hfi-link-native.s')])
    hfi_assembly=(out/'hfi-link-native.s').read_text()
    for marker in ('cepheus_gmu_hfi_bind:', 'cepheus_gmu_hfi_poll_debug:', 'dsb\tsy'):
        if marker not in hfi_assembly:raise SystemExit('Missing HFI native marker: '+marker)
    stack=(out/'hfi-link-native.su').read_text()
    receipt['hfi_native_stack_usage']=stack
    for row in stack.splitlines():
        fields=row.split('\t')
        if len(fields)!=3 or fields[2]!='static' or int(fields[1])>4096:
            raise SystemExit('Unexpected HFI native stack usage: '+row)
    run('hfi-link-native-analysis', [*command, '--analyze', '-Xanalyzer', '-analyzer-output=text',
        str(ROOT/'xnu/cepheus_gmu_hfi_link.c'), '-o', os.devnull])
    assembly = (out/'native.s').read_text()
    for marker in ('cepheus_gpu_gpll0_hold:', 'cepheus_gpu_gpll0_check:', 'cepheus_gpu_gpll0_measure:',
                   'cepheus_gpu_power_start_native:',
                   'cepheus_gpu_tsens_bind_native:','cepheus_gpu_temperature_sample_native:','cepheus_gpu_thermal_now_native:',
                   'cepheus_gmu_start_cold_native:',
                   'cepheus_gpu_pdc_install_native:',
                   'cepheus_gpu_smmu_install_native:',
                   'cepheus_gmu_upload_native:',
                   'cepheus_gpu_qmp_acd_native:',
                   'cepheus_gpu_oob_acquire_native:', 'cepheus_gpu_oob_release_native:',
                   'cepheus_gmu_hfi_version_native:',
                   'cepheus_gmu_hfi_boot_native:','cepheus_gmu_hfi_dcvs_native:','cepheus_gmu_hfi_start_native:',
                   'cepheus_gpu_gx_core_native:','cepheus_gpu_gx_setup_native:',
                   'cepheus_gpu_gx_setup_perf_native:',
                   'cepheus_gpu_gx_smmu_install_native:',
                   'cepheus_gpu_cp_prepare_native:',
                   'cepheus_gpu_cp_ring_native:',
                   'cepheus_gpu_cp_execution_admit:','cepheus_gpu_cp_execution_check:',
                   'dsb\tosh', 'ml_io_map', 'delay', 'mach_absolute_time', 'absolutetime_to_nanoseconds'):
        if marker not in assembly:
            raise SystemExit('Missing actual native assembly marker: '+marker)
    run('native-analysis', [*command, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                            str(file), '-o', os.devnull])
    run('host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                          str(ROOT/'xnu/test_gpll0.c'), '-o', os.devnull])
    run('measure-host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                                 str(ROOT/'xnu/test_clock_measure.c'), '-o', os.devnull])
    for name in ('cp_ring_native','cp_native','gx_smmu_native','gx_core_native','gpu_config_native','hfi_session','hfi_access','gmu_oob','gmu_oob_native','qmp_native','qmp_mmio','qmp_connect','gpu_power_native','gpu_power_mmio','gpu_tsens','gpu_tsens_native','gmu_start_native','gmu_mmio','gpu_pdc_native','pdc_mmio','smmu_native','smmu_mmio','gmu_upload_native','gmu_upload','gmu_upload_mmio'):
        run(name+'-host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                                   str(ROOT/'xnu'/('test_'+name+'.c')), '-o', os.devnull])
    run('hfi-link-host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
        str(ROOT/'tests/test_hfi_link_access.c'), '-o', os.devnull])
    run('hfi-native-host-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
        str(ROOT/'xnu/test_gmu_hfi_native.c'),'-o',os.devnull])
    run('hfi-native-pool-host-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
        str(ROOT/'xnu/test_gmu_hfi_native_pool.c'),'-o',os.devnull])
    run('cp-execution-admission-host-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
        str(ROOT/'tests/test_cp_execution_admission.c'),'-o',os.devnull])
    run('cp-execution-admission-native-host-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
        str(ROOT/'tests/test_cp_execution_admission_native.c'),'-o',os.devnull])
    run('owner-evidence-host-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
        str(ROOT/'xnu/test_owner_evidence.c'),'-o',os.devnull])
    run('zap-resource-evidence-host-analysis',['clang',*common,'--analyze','-Xanalyzer','-analyzer-output=text',
        str(ROOT/'xnu/test_zap_resource_evidence.c'),'-o',os.devnull])
    # Stage exactly as the existing local/CI copy manifests do, then compile
    # only from that tree. This catches missing transitive header copies.
    module_spec = importlib.util.spec_from_file_location('verification', ROOT/'scripts/verify-gpu-kernel.py')
    verification = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(verification)
    patches = out/'patches'
    patches.mkdir()
    for name in verification.HEADERS:
        shutil.copyfile(ROOT/'xnu'/name, patches/name)
    script = patches/'0087-stage-cepheus-gpu-memory.sh'
    shutil.copyfile(ROOT/'xnu'/script.name, script)
    for name in ('cepheus_gpu_memory.cpp', 'cepheus_gpu_memory_state.h'):
        shutil.copyfile(ROOT/'xnu'/name, patches/name)
    tree = out/'staged'
    (tree/'bsd/dev').mkdir(parents=True)
    (tree/'bsd/conf').mkdir()
    (tree/'bsd/conf/files').write_text('base\n')
    with (out/'staging.log').open('w') as stream:
        subprocess.run(['bash', str(script)], cwd=tree, stdout=stream, stderr=subprocess.STDOUT, check=True)
    for name in ('cepheus_gpll0.h', 'cepheus_gpll0_private.h', 'cepheus_clock_measure.h',
                 'cepheus_clock_measure_private.h', 'cepheus_gpu_compile.c', 'cepheus_gpu_compile.h',
                 'cepheus_gpu_power_native.h','cepheus_gpu_power_native_private.h','cepheus_gpu_power_mmio.h'):
        if (tree/'bsd/dev'/name).read_bytes() != (ROOT/'xnu'/name).read_bytes():
            raise SystemExit('Staged source mismatch: '+name)
    native_source = (tree/'bsd/dev/cepheus_gpu_compile.c').read_text()
    for name in ('cepheus_gpu_tsens.h','cepheus_gpu_tsens_native.h','cepheus_gpu_tsens_native_private.h',
                 'cepheus_gmu_oob_native.h','cepheus_gmu_oob_native_private.h',
                 'cepheus_gmu_start_native.h','cepheus_gmu_start_native_private.h','cepheus_gmu_mmio.h',
                 'cepheus_gpu_pdc_native.h','cepheus_gpu_pdc_native_private.h','cepheus_pdc_mmio.h',
                 'cepheus_smmu_native.h','cepheus_smmu_native_private.h','cepheus_smmu_mmio.h','cepheus_gpu_memory_private.h',
                 'cepheus_gmu_upload_native.h','cepheus_gmu_upload_native_private.h','cepheus_gmu_upload.h','cepheus_gmu_upload_mmio.h'):
        if (tree/'bsd/dev'/name).read_bytes()!=(ROOT/'xnu'/name).read_bytes():
            raise SystemExit('Staged native source mismatch: '+name)
    file = out/'staged-native.c'
    file.write_text(declarations+'static LCK_GRP_DECLARE'+
                    native_source.split('static LCK_GRP_DECLARE', 1)[1].split('\n#endif', 1)[0])
    run('staged-native-compile', ['clang', '--target=aarch64-none-elf', '-O2', '-ffreestanding',
                                 '-std=c11', '-Wall', '-Wextra', '-Werror',
                                 '-I', str(tree/'bsd/dev'), '-c', str(file), '-o', str(out/'staged-native.o')])
    for name in ('cepheus_hfi.h','cepheus_hfi_session.h','cepheus_gmu_hfi_link.c','cepheus_gmu_hfi_link.h',
                 'cepheus_gmu_hfi_native.h','cepheus_gmu_hfi_native_private.h',
                 'cepheus_gpu_config_native.h','cepheus_gpu_config_native_private.h'):
        if (tree/'bsd/dev'/name).read_bytes()!=(ROOT/'xnu'/name).read_bytes():
            raise SystemExit('Staged HFI source mismatch: '+name)
    run('staged-hfi-link-native-compile', ['clang', '--target=aarch64-none-elf', '-O2', '-ffreestanding',
        '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(tree/'bsd/dev'),
        '-c',str(tree/'bsd/dev/cepheus_gmu_hfi_link.c'),'-o',str(out/'staged-hfi-link-native.o')])
    if args.gmu:
        fixtures={'gmu':args.gmu.resolve(),'sqe':args.sqe.resolve()}
        receipt['proprietary_fixtures']={name:{'path':str(path),'bytes':path.stat().st_size,
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest()} for name,path in fixtures.items()}
        env=os.environ.copy()
        env['CEPHEUS_GPU_HOST_ASAN']='1'
        env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1'
        env.pop('CEPHEUS_ZAP_ELF',None)
        run('real-firmware-memory-chain',['bash','scripts/test-gpu-memory-host.sh',
            str(fixtures['gmu']),str(fixtures['sqe'])],env,timeout=900)
        run('real-firmware-gx-smmu-chain',['bash','scripts/test-gx-smmu-native-pool.sh',
            str(fixtures['gmu']),str(fixtures['sqe']),str(out/'gx-smmu-pool')],env,timeout=900)
        run('real-firmware-cp-build-chain',['bash','scripts/test-cp-build-pool.sh',
            str(fixtures['sqe']),str(out/'cp-build-pool')],env,timeout=120)
        run('real-firmware-cp-ring-chain',['bash','scripts/test-cp-ring-native-pool.sh',
            str(fixtures['gmu']),str(fixtures['sqe']),str(out/'cp-ring-pool')],env,timeout=900)
        for name,path in fixtures.items():
            if hashlib.sha256(path.read_bytes()).hexdigest()!=receipt['proprietary_fixtures'][name]['sha256']:
                raise SystemExit('Firmware fixture changed during acceptance: '+name)
    current={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in names}
    if current!=receipt['source_sha256']:raise SystemExit('Source changed during acceptance')
    receipt['source_unchanged']=True
    receipt['result'] = 'PASS'
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('GPLL0/counter/native power/TSENS/CM3/PDC/SMMU/upload/QMP/OOB/HFI local acceptance PASS; no phone measurement or GPU execution')
    if args.output:
        print(out/'receipt.json')
    if temporary:
        temporary.cleanup()


if __name__ == '__main__':
    main()
