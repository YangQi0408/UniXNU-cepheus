#!/usr/bin/env python3
"""Compile pinned vendor DTS with labeled stubs and audit TBU coverage, offline."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PIN = '36f369fdb77319a2984a48b89f025ac3e5f97386'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT/'scripts'/filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out, kernel = args.output.resolve(), args.kernel.resolve()
    if out.exists():
        parser.error('output exists; preserve earlier evidence')
    head = subprocess.check_output(['git', '-C', str(kernel), 'rev-parse', 'HEAD'], text=True).strip()
    assert head == PIN, 'vendor source pin'
    relative = ['drivers/iommu/arm-smmu.c',
                'arch/arm64/boot/dts/qcom/msm-arm-smmu-sm8150.dtsi',
                'arch/arm64/boot/dts/qcom/sm8150-gpu.dtsi',
                'include/dt-bindings/clock/qcom,gcc-sm8150.h',
                'include/dt-bindings/clock/qcom,gpucc-sm8150.h',
                'include/dt-bindings/clock/qcom,cpucc-sm8150.h',
                'include/dt-bindings/regulator/qcom,rpmh-regulator.h',
                'include/dt-bindings/interrupt-controller/arm-gic.h',
                'include/dt-bindings/interrupt-controller/irq.h',
                'include/dt-bindings/msm/msm-bus-ids.h']
    source = {}
    for name in relative:
        data = (kernel/name).read_bytes()
        recorded = subprocess.check_output(['git', '-C', str(kernel), 'show', PIN+':'+name])
        assert data == recorded, 'dirty source: '+name
        source[name] = hashlib.sha256(data).hexdigest()
    out.mkdir(parents=True)
    receipt = {'status': 'RUNNING', 'vendor_commit': PIN, 'source_sha256': source,
               'phone_operations': False, 'gpu_execution': False, 'resource_owner': False,
               'steps': [], 'local_sha256': {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
                   for name in ('scripts/gpu-fdt-config.py', 'scripts/audit-smmu-tbu-source.py',
                                'scripts/audit-pas13-source.py', 'tests/smmu-topology-source.dts')},
               'limitations': ['vendor source with synthetic external label stubs, NOT live board FDT',
                               'no phone/SMC/MMIO, TBU halt or resource requests',
                               'topology and destructive-debug source audit, NOT runtime ownership']}

    def save():
        (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')

    def run(command, stdout, stderr):
        with (out/stdout).open('wb') as output, (out/stderr).open('wb') as errors:
            result = subprocess.run(command, stdout=output, stderr=errors, timeout=60)
        receipt['steps'].append({'command': command, 'exit_code': result.returncode,
                                 'stdout': stdout, 'stderr': stderr})
        save()
        if result.returncode:
            raise ValueError((out/stderr).read_text())

    try:
        run([os.environ.get('CC', 'cc'), '-E', '-P', '-x', 'assembler-with-cpp', '-nostdinc',
             '-undef', '-D__DTS__', '-I'+str(kernel/'include'),
             '-I'+str(kernel/'arch/arm64/boot/dts/qcom'),
             str(ROOT/'tests/smmu-topology-source.dts')], 'source.dts', 'cpp.log')
        run(['dtc', '-I', 'dts', '-O', 'dtb', '-o', str(out/'source.dtb'), str(out/'source.dts')],
            'dtc.stdout', 'dtc.log')
        fdt = module('tbu_fdt', 'gpu-fdt-config.py')
        nodes = fdt.parse((out/'source.dtb').read_bytes())
        handles = {fdt.cells(props['phandle'])[0]: path for path, props in nodes.items() if 'phandle' in props}
        streams = {}
        smmu = None
        for path, props in nodes.items():
            name = path.rsplit('/', 1)[-1]
            if name not in ('gmu_kernel', 'gmu_user', 'gfx3d_user', 'gfx3d_secure'):
                continue
            spec = fdt.cells(props['iommus'])
            assert len(spec) == 3 and spec[0] in handles
            target = handles[spec[0]]
            assert smmu is None or smmu == target
            smmu = target
            streams[name] = {'sid': spec[1], 'mask': spec[2]}
        assert set(streams) == {'gmu_kernel', 'gmu_user', 'gfx3d_user', 'gfx3d_secure'}
        topology = fdt.tbu_coverage(nodes, smmu, streams)
        expected = {'gmu_kernel': [5, 0x405], 'gmu_user': [4, 0x404],
                    'gfx3d_user': [0, 1, 0x400, 0x401], 'gfx3d_secure': [2, 0x402]}
        assert {name: row['matched_sids'] for name, row in topology['stream_coverage'].items()} == expected
        assert all(len(row['tbus']) == 2 for row in topology['stream_coverage'].values())
        assert [tbu['registers'] for tbu in topology['tbus']] == [
            {'base': {'address': 0x2cc5000, 'bytes': 0x1000}, 'status-reg': {'address': 0x2cc2200, 'bytes': 8}},
            {'base': {'address': 0x2cc9000, 'bytes': 0x1000}, 'status-reg': {'address': 0x2cc2208, 'bytes': 8}}]
        assert 'qcom,skip-init' in nodes[smmu] and 'qcom,use-3-lvl-tables' in nodes[smmu]
        assert fdt.strings(nodes[smmu]['clock-names']) == [
            'gcc_gpu_memnoc_gfx_clk', 'gcc_gpu_snoc_dvm_gfx_clk', 'gpu_cc_ahb_clk']
        supply = handles[fdt.cells(nodes[smmu]['vdd-supply'])[0]]
        assert supply == '/soc/gpu-cx'
        audit = module('tbu_body', 'audit-pas13-source.py')
        driver = (kernel/'drivers/iommu/arm-smmu.c').read_text()
        find = audit.code(audit.body(driver, 'qsmmuv500_find_tbu'))
        assert 'tbu->sid_start <= sid' in find and 'sid < tbu->sid_start + tbu->num_sids' in find
        match = audit.code(audit.body(driver, 'arm_smmu_fwspec_match_smr'))
        assert '(smr->id ^ smr2->id) & ~(smr->mask | smr2->mask)' in match
        halt = audit.code(audit.body(driver, 'qsmmuv500_tbu_halt'))
        assert all(marker in halt for marker in ('writel_relaxed', 'ARM_SMMU_CB_FSR',
            'ARM_SMMU_CB_SCTLR', 'RESUME_TERMINATE', 'halt &= ~DEBUG_SID_HALT_VAL'))
        receipt.update(status='PASS', topology=topology, parent_cx_supply_label=supply,
                       parent_clock_names=fdt.strings(nodes[smmu]['clock-names']),
                       source_dtb_sha256=hashlib.sha256((out/'source.dtb').read_bytes()).hexdigest(),
                       destructive_debug_halt_verified=True)
        save()
        print('Pinned source TBU coverage and destructive-debug audit PASS; not owner/runtime: '+str(out/'receipt.json'))
        return 0
    except Exception as error:
        receipt.update(status='FAIL', error=str(error))
        save()
        raise


if __name__ == '__main__':
    sys.exit(main())
