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
    args = parser.parse_args()
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

    def run(label, command, env=None):
        log = out/(label+'.log')
        with log.open('w') as stream:
            code = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                  env=env, timeout=120).returncode
        body = log.read_text()
        if 'warning:' in body or 'error:' in body:
            code = code or 1
        receipt['steps'].append({'label': label, 'command': command, 'exit_code': code,
                                 'log': log.name, 'sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
        receipt['result'] = 'FAIL' if code else 'RUNNING'
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
        print(label, 'FAIL' if code else 'PASS', flush=True)
        if code:
            raise SystemExit(body)

    common = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'xnu')]
    for name, compiler, flags in (
        ('host', os.environ.get('CC', 'cc'), ['-O2']),
        ('sanitized', 'clang', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']),
    ):
        exe = out/name
        run(name+'-compile', [compiler, *common, *flags, str(ROOT/'xnu/test_gpll0.c'), '-o', str(exe)])
        env = os.environ.copy()
        # Apple ASan has no LeakSanitizer; keep address/UB checks on both hosts.
        env['ASAN_OPTIONS'] = ('detect_leaks=0' if sys.platform=='darwin' else 'detect_leaks=1')+':halt_on_error=1'
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
        for sensor in ('gpu_tsens','gpu_tsens_native','gmu_start_native','gmu_mmio','gpu_pdc_native','pdc_mmio','smmu_native','smmu_mmio','gmu_upload_native','gmu_upload','gmu_upload_mmio'):
            exe=out/(name+'-'+sensor)
            run(name+'-'+sensor+'-compile', [compiler,*common,*flags,str(ROOT/'xnu'/('test_'+sensor+'.c')),'-o',str(exe)])
            run(name+'-'+sensor+'-run',[str(exe)],env)
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
    assembly = (out/'native.s').read_text()
    for marker in ('cepheus_gpu_gpll0_hold:', 'cepheus_gpu_gpll0_check:', 'cepheus_gpu_gpll0_measure:',
                   'cepheus_gpu_power_start_native:',
                   'cepheus_gpu_tsens_bind_native:','cepheus_gpu_temperature_sample_native:','cepheus_gpu_thermal_now_native:',
                   'cepheus_gmu_start_cold_native:',
                   'cepheus_gpu_pdc_install_native:',
                   'cepheus_gpu_smmu_install_native:',
                   'cepheus_gmu_upload_native:',
                   'dsb\tosh', 'ml_io_map', 'delay', 'mach_absolute_time', 'absolutetime_to_nanoseconds'):
        if marker not in assembly:
            raise SystemExit('Missing actual native assembly marker: '+marker)
    run('native-analysis', [*command, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                            str(file), '-o', os.devnull])
    run('host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                          str(ROOT/'xnu/test_gpll0.c'), '-o', os.devnull])
    run('measure-host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                                 str(ROOT/'xnu/test_clock_measure.c'), '-o', os.devnull])
    for name in ('gpu_power_native','gpu_power_mmio','gpu_tsens','gpu_tsens_native','gmu_start_native','gmu_mmio','gpu_pdc_native','pdc_mmio','smmu_native','smmu_mmio','gmu_upload_native','gmu_upload','gmu_upload_mmio'):
        run(name+'-host-analysis', ['clang', *common, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                                   str(ROOT/'xnu'/('test_'+name+'.c')), '-o', os.devnull])
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
    receipt['result'] = 'PASS'
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('GPLL0/counter/native power/TSENS/CM3/PDC/SMMU/upload local acceptance PASS; no phone measurement or GPU execution')
    if args.output:
        print(out/'receipt.json')
    if temporary:
        temporary.cleanup()


if __name__ == '__main__':
    main()
