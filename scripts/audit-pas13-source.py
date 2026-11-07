#!/usr/bin/env python3
"""Audit saved SM8150 PAS13 source/FDT evidence; never invoke SMC or a device.

Writes a new local report only. These are source assertions, not secure firmware
semantics, runtime ownership, authentication, or GPU execution tests.
"""
import argparse
from collections import deque
import hashlib
import importlib.util
import json
from pathlib import Path
import re


def identity(path):
    data = path.read_bytes()
    return {'path': str(path), 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest()}


def code(text):
    # Preserve offsets while masking comments and strings for function boundaries
    # and negative call-site assertions (comments are not executable evidence).
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern, lambda m: ' ' * len(m[0]), text, flags=re.S)


def body(text, name):
    masked = code(text)
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', masked)
    if not match:
        raise ValueError('function definition missing: ' + name)
    start = match.end() - 1
    depth = 0
    for pos in range(start, len(masked)):
        depth += (masked[pos] == '{') - (masked[pos] == '}')
        if depth == 0:
            return text[start:pos + 1]
    raise ValueError('unclosed function: ' + name)


def crypto_route(fdt, nodes, src, dst):
    handles = {fdt.cells(v['phandle'])[0]: p for p, v in nodes.items() if 'phandle' in v}
    ids = {}
    for path, props in nodes.items():
        if path.startswith('/soc/ad-hoc-bus/') and 'cell-id' in props:
            value = fdt.cells(props['cell-id'])
            if len(value) != 1 or value[0] in ids:
                raise ValueError('duplicate/non-scalar bus cell-id')
            ids[value[0]] = path
    if src not in ids or dst not in ids:
        raise ValueError('crypto bus endpoint missing')
    queue = deque([[ids[src]]])
    found = []
    steps = 0
    while queue:
        path = queue.popleft()
        steps += 1
        if steps > 16384 or len(path) > 32:
            raise ValueError('crypto route exceeds traversal budget')
        if path[-1] == ids[dst]:
            found.append(path)
            continue
        for handle in fdt.cells(nodes[path[-1]]['qcom,connections']) if 'qcom,connections' in nodes[path[-1]] else []:
            child = handles[handle]
            if child not in path:
                queue.append(path + [child])
    if len(found) != 1:
        raise ValueError('missing/ambiguous crypto route')
    result = []
    for path in found[0]:
        props = nodes[path]
        resources = []
        for handle in fdt.cells(props['qcom,bcms']) if 'qcom,bcms' in props else []:
            target = handles[handle]
            rscs = fdt.cells(nodes[target]['qcom,rscs'])
            names = fdt.strings(nodes[target]['qcom,bcm-name'])
            if len(names) != 1 or len(rscs) != 1:
                raise ValueError('ambiguous BCM/RSC')
            rsc = handles[rscs[0]]
            resources.append({'name': names[0], 'path': target, 'rsc_path': rsc,
                              'rsc_id': fdt.cells(nodes[rsc]['cell-id'])[0]})
        result.append({'node': path, 'buswidth': fdt.cells(props['qcom,buswidth'])[0],
                       'ports': fdt.cells(props['qcom,agg-ports'])[0], 'bcms': resources})
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', required=True, type=Path)
    p.add_argument('--fdt', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--config', type=Path)
    p.add_argument('--uefi-header', type=Path)
    p.add_argument('--boot-result', type=Path)
    a = p.parse_args()
    if a.output.exists():
        p.error('output already exists; preserve earlier evidence')
    files = {}
    def source(relative):
        path = a.kernel / relative
        files[relative] = identity(path)
        return path.read_text()
    kgsl = source('drivers/gpu/msm/adreno_a6xx.c')
    ssr = source('drivers/soc/qcom/subsystem_restart.c')
    pil = source('drivers/soc/qcom/peripheral-loader.c')
    tz = source('drivers/soc/qcom/subsys-pil-tz.c')
    scm = source('drivers/soc/qcom/scm.c')
    wrapper = source('drivers/firmware/qcom_scm.c')
    pas = source('drivers/firmware/qcom_scm-64.c')
    ids = source('include/dt-bindings/msm/msm-bus-ids.h')
    checks = []
    def check(name, condition):
        if not condition:
            raise ValueError('source evidence mismatch: ' + name)
        checks.append({'name': name, 'status': 'PASS'})
    load = body(kgsl, 'a6xx_microcode_load')
    check('KGSL secure-MMU branch precedes subsystem_get',
          load.index('if (!device->mmu.secured)') < load.index('subsystem_get('))
    check('KGSL checks ZAP load failure', 'IS_ERR_OR_NULL' in load and 'PTR_ERR' in load)
    check('SSR get starts subsystem; start calls powerup',
          'subsys_start(subsys)' in body(ssr, '__subsystem_get') and
          'subsys->desc->powerup(subsys->desc)' in body(ssr, 'subsys_start'))
    check('PIL-TZ registers trusted ops and powerup calling pil_boot',
          'd->desc.ops = &pil_ops_trusted' in body(tz, 'pil_tz_driver_probe') and
          'd->subsys_desc.powerup = subsys_powerup' in body(tz, 'pil_tz_driver_probe') and
          'pil_boot(&d->desc)' in body(tz, 'subsys_powerup'))
    for name, text in (('KGSL', kgsl), ('SSR', ssr), ('PIL', pil), ('PIL-TZ', tz)):
        check(name + ' has no PAS supported7/availability admission call', not re.search(
            r'\b(?:qcom_scm_pas_supported|__qcom_scm_pas_supported|scm_is_call_available|'
            r'qcom_scm_is_call_available|__qcom_scm_is_call_available|'
            r'PAS_IS_SUPPORTED_CMD|QCOM_SCM_PAS_IS_SUPPORTED_CMD)\b', code(text)))
    boot = body(pil, 'pil_boot')
    order = ['pil_init_mmap(', 'pil_proxy_vote(', 'desc->ops->init_image(',
             'desc->ops->mem_setup(', 'pil_load_segs(', 'desc->ops->auth_and_reset(']
    positions = [boot.index(token) for token in order]
    check('PIL map/proxy/init/mem/load/auth order', positions == sorted(positions))
    init = body(tz, 'pil_init_image_trusted')
    auth = body(tz, 'pil_auth_and_reset')
    check('PAS init requires crypto BW before DMA/SMC',
          init.index('scm_pas_enable_bw()') < init.index('dma_alloc_attrs(') < init.index('scm_call2('))
    check('PAS auth requires crypto BW before SMC',
          auth.index('scm_pas_enable_bw()') < auth.index('scm_call2('))
    check('No crypto bus client fails before SMC',
          re.search(r'if\s*\(!scm_perf_client\)\s*return -EINVAL;', body(tz, 'scm_pas_enable_bw')) is not None)
    check('PAS probe defaults to CRYPTO_CORE_0, optional mas-crypto override',
          'crypto_id = MSM_BUS_MASTER_CRYPTO_CORE_0' in body(tz, 'pil_tz_driver_probe') and
          '"qcom,mas-crypto"' in body(tz, 'pil_tz_driver_probe') and
          'scm_pas_init((int)crypto_id)' in body(tz, 'pil_tz_driver_probe'))
    check('PAS crypto table is separate shared DDR route',
          'MSM_BUS_SLAVE_EBI_CH0' in tz and '492 * 8 * 1000000UL' in tz and
          '492 * 8 *  100000UL' in tz and 'static uint32_t scm_perf_client' in tz)
    supported = body(pas, '__qcom_scm_pas_supported')
    check('Generic supported wrapper queries command7 availability first',
          'QCOM_SCM_PAS_IS_SUPPORTED_CMD' in body(wrapper, 'qcom_scm_pas_supported') and
          '__qcom_scm_is_call_available' in body(wrapper, 'qcom_scm_pas_supported'))
    check('Generic supported uses one VAL and returns false for a1=0',
          'QCOM_SCM_ARGS(1)' in supported and 'return ret ? false : !!res.a1;' in supported)
    check('Supported body has no crypto vote or clock enable',
          not re.search(r'\b(?:scm_pas_enable_bw|qcom_scm_clk_enable)\b', code(supported)))
    check('Downstream ordinary call uses discovered mask, not forced FAST',
          'x0 = fn_id | scm_version_mask;' in scm and
          '__scm_call_armv8_64(x0 | SMC64_MASK, SCM_ARGS(1), x0' in body(scm, 'is_scm_armv8'))
    spec = importlib.util.spec_from_file_location('saved_fdt', Path(__file__).with_name('gpu-fdt-config.py'))
    fdt = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fdt)
    blob = a.fdt.read_bytes()
    profile = fdt.report(blob)  # Validate bounded FDT and existing GPU identities first.
    nodes = fdt.parse(blob)
    zap = nodes[profile['zap']['path']]
    check('Saved GPU ZAP is authenticated PAS13, no crypto phandle override',
          profile['zap']['pas_id'] == 13 and not profile['zap']['no_auth'] and 'qcom,mas-crypto' not in zap)
    def macro(name):
        return int(re.search(r'#define\s+' + name + r'\s+(\d+)\b', ids)[1])
    src, dst = macro('MSM_BUS_MASTER_CRYPTO_CORE_0'), macro('MSM_BUS_SLAVE_EBI_CH0')
    route = crypto_route(fdt, nodes, src, dst)
    optional = {}
    if a.config:
        config = a.config.read_text()
        check('Local config selects KGSL/downstream PIL; remoteproc disabled', all(x in config.splitlines() for x in (
            'CONFIG_QCOM_KGSL=y', 'CONFIG_QCOM_SCM=y', 'CONFIG_QCOM_SCM_64=y',
            'CONFIG_MSM_PIL=y', 'CONFIG_MSM_PIL_SSR_GENERIC=y', '# CONFIG_REMOTEPROC is not set')))
        optional['config'] = identity(a.config)
    if a.uefi_header:
        header = a.uefi_header.read_text()
        check('UEFI supported7 declares one VAL and boolean verdict', all(x in header for x in (
            'TZ_SYSCALL_CREATE_SMC_ID (TZ_OWNER_SIP, TZ_SVC_PIL, 0x07)',
            'TZ_PIL_IS_SUBSYS_SUPPORTED_ID_PARAM_ID', 'UINT64 verdict;', 'FALSE')))
        optional['uefi_header'] = identity(a.uefi_header)
    if a.boot_result:
        live = json.loads(a.boot_result.read_text())
        transcript = live['scm_register_transcript']
        check('Saved boot transcript: ordinary SMC64 PAS13 supported=false, no mutation',
              len(transcript) == 6 and transcript[-1]['in'][:3] == ['0x42000207', '0x1', '0xd'] and
              transcript[-1]['out'][:2] == ['0', '0'] and live['pas_mutation_executed'] is False)
        optional['boot_result'] = identity(a.boot_result)
    report = {'scope': 'OFFLINE source/saved-FDT audit only; no SMC, auth, power or phone operation',
              'sources': files, 'fdt': identity(a.fdt), 'optional_inputs': optional,
              'checks': checks, 'result': 'PASS', 'zap': profile['zap'],
              'crypto_bandwidth': {'source_id': src, 'destination_id': dst,
                  'active_ab_bytes_per_second': 492 * 8 * 100000,
                  'active_ib_bytes_per_second': 492 * 8 * 1000000, 'route': route},
              'conclusion': 'saved supported7=false remains; downstream GPU has no supported7 precheck; XNU admission remains closed',
              'limitations': ['Source assertions are not a complete binary call graph or shipped Android runtime trace',
                  'Cause of supported7=false and actual PAS13 authentication capability remain UNKNOWN',
                  'Crypto topology/table do not grant vote, memory, XPU or shutdown ownership',
                  'UEFI protocol/headers do not prove post-ExitBootServices ABI or secure implementation']}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x') as output:
        output.write(json.dumps(report, indent=2) + '\n')
    print(f'PAS13 saved-source audit: {len(checks)} checks PASS; no SMC; {a.output}')


if __name__ == '__main__':
    main()
