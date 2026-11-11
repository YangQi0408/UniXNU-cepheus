#!/usr/bin/env python3
"""Portable GPU source/staging integration checks, not XNU ABI or runtime tests."""
from pathlib import Path
import importlib.util
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Integration(unittest.TestCase):
    def test_smem_map_device_runner_is_scoped_and_mock_tested(self):
        source = (ROOT / 'scripts/run-smem-map-device.py').read_text()
        self.assertIn('diagnostic=Diagnostic', source)
        self.assertIn("'mapping_diagnostic_pass': None", source)
        self.assertIn("analysis == LOG.analyze(raw)", source)
        self.assertIn("analysis.get('build_metadata') == EXPECTED_BUILD_METADATA", source)
        for command in ('flash', 'erase', 'reboot', 'set_active', 'format'):
            self.assertNotIn(f"'{command}'", source)
        subprocess.run(['python3', str(ROOT / 'scripts/test-run-smem-map-device.py')], check=True)

    def test_rpmh_tail_device_runner_is_boot_only(self):
        source = (ROOT / 'scripts/run-rpmh-tail-device.py').read_text()
        self.assertIn("['fastboot', '-s', serial, 'boot', str(image)]", source)
        for command in ('flash', 'erase', 'reboot', 'set_active', 'format'):
            self.assertNotIn(f"'{command}'", source)
        for call in ('cepheus_gpu_arc_raise', 'cepheus_gpu_bcm_raise',
                     'cepheus_gpu_submit', 'cepheus_gpu_cp_execution_admit'):
            self.assertNotIn(call, source)
        subprocess.run(['python3', str(ROOT / 'scripts/test-run-rpmh-tail-device.py')], check=True)

    def test_scm_pair_device_runner_is_boot_only(self):
        source = (ROOT / 'scripts/run-scm-pair-device.py').read_text()
        self.assertIn("boot_command(serial, identity['path'])", source)
        self.assertEqual(source.count('subprocess.run(boot_command('), 1)
        self.assertIn("'boot_attempt_count': 0", source)
        for command in ('flash', 'erase', 'reboot', 'set_active', 'format'):
            self.assertNotIn(f"'{command}'", source)
        for call in ('cepheus_gpu_arc_raise', 'cepheus_gpu_bcm_raise',
                     'cepheus_gpu_submit', 'cepheus_gpu_cp_execution_admit'):
            self.assertNotIn(call, source)
        subprocess.run(['python3', str(ROOT / 'scripts/test-run-scm-pair-device.py')], check=True)

    def test_offline_preflight_allows_measured_sanitizer_runtime(self):
        path = ROOT / 'scripts/validate-gpu-offline.py'
        spec = importlib.util.spec_from_file_location('offline_preflight', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.CHECK_TIMEOUT_SECONDS['sanitizers'], 900)
        self.assertEqual(module.CHECK_TIMEOUT_SECONDS['memory-real-fixtures'], 600)
        self.assertEqual(module.DEFAULT_CHECK_TIMEOUT_SECONDS, 240)
        self.assertIn('timeout=timeout', path.read_text())

    def test_workflow_copies_every_statically_staged_source(self):
        stage = (ROOT / 'xnu/0087-stage-cepheus-gpu-memory.sh').read_text()
        workflow = (ROOT / '.github/workflows/touch-kernel.yml').read_text()
        required = set(re.findall(r'\$here/(cepheus_[A-Za-z0-9_]+\.(?:c|h|cpp))(?![\w.])', stage))
        self.assertTrue(required)
        missing = sorted(name for name in required if name not in workflow)
        self.assertEqual(missing, [], f'workflow omits staged sources: {missing}')

    def test_failed_kernel_evidence_has_job_log_fallback(self):
        workflow = (ROOT / '.github/workflows/touch-kernel.yml').read_text()
        name = '      - name: Retain unaccepted kernel and disassembly after verification failure\n'
        self.assertEqual(workflow.count(name), 1)
        step = workflow.split(name, 1)[1].split('      - uses:', 1)[0]
        self.assertIn('        if: failure()\n', step)
        self.assertIn('UNACCEPTED: offline debugging only; verification failed; never boot', step)
        self.assertIn('cat unaccepted/UNACCEPTED.txt unaccepted/device-commit.txt unaccepted/SHA256SUMS', step)
        producer = '--disassemble-symbols=_soc_smem_map,_soc_thread "$K" > unaccepted/smem-receiver.asm'
        self.assertIn(producer, step)
        self.assertGreater(step.index('cat unaccepted/smem-receiver.asm'), step.index(producer))
        shell = step.split('        run: |\n', 1)[1]
        shell = '\n'.join(line[10:] for line in shell.splitlines())
        subprocess.run(['bash', '-n'], input=shell, text=True, check=True)

    def test_soc_canonical_tail_headers_staged(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);patches=root/'patches';patches.mkdir()
            tree=root/'tree';(tree/'bsd/conf').mkdir(parents=True)
            (tree/'bsd/dev').mkdir();(tree/'bsd/conf/files').write_text('base\n')
            (tree/'osfmk/arm64').mkdir(parents=True)
            names=('cepheus_rpmh_snapshot.h','cepheus_rpmh_tail.h','cepheus_rpmh_tail_handoff.h')
            for name in ('0085-stage-cepheus-soc.sh','cepheus_soc.c','cepheus_soc.h',
                         'cepheus_cpu_policy.h','cepheus_gpu_power.h',
                         'cepheus_gpu_idle.h','cepheus_gpu_idle_native_private.h','cepheus_smem_map_handoff.h'):
                shutil.copyfile(ROOT/'xnu'/name,patches/name)
            for name in names:shutil.copyfile(ROOT/'firmware'/name,patches/name)
            subprocess.run(['bash',str(patches/'0085-stage-cepheus-soc.sh')],cwd=tree,check=True)
            for name in names:
                self.assertEqual((tree/'bsd/dev'/name).read_bytes(),(ROOT/'firmware'/name).read_bytes())
            for name in ('cepheus_gpu_idle.h','cepheus_gpu_idle_native_private.h'):
                self.assertEqual((tree/'osfmk/arm64'/name).read_bytes(),(ROOT/'xnu'/name).read_bytes())
            self.assertEqual((tree/'bsd/dev/cepheus_smem_map_handoff.h').read_bytes(),
                             (ROOT/'xnu/cepheus_smem_map_handoff.h').read_bytes())

    def test_real_config_sources_when_supplied(self):
        # Optional explicit reference; synthetic coverage is not presented as
        # real source application. CI verifies the actual applied build tree.
        import os
        source = os.environ.get('CEPHEUS_XNU_REFERENCE')
        if not source:
            self.skipTest('set CEPHEUS_XNU_REFERENCE for actual pre-GPU conf files')
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            subprocess.run(['git', 'init', '-q', str(tree)], check=True)
            for name in ('bsd/conf/files', 'iokit/conf/files.arm64'):
                (tree / name).parent.mkdir(parents=True, exist_ok=True)
                (tree / name).write_bytes((Path(source) / name).read_bytes())
            conf = (tree / 'bsd/conf/files').read_text()
            patches = []
            if 'bsd/dev/cepheus_touch.c' not in conf: patches.append('0082-cepheus-touch.patch')
            if 'bsd/dev/cepheus_soc.c' not in conf: patches.append('0085-cepheus-soc.patch')
            for patch in patches + ['0087-cepheus-gpu-memory.patch']:
                subprocess.run(['git', '-C', str(tree), 'apply', str(ROOT / 'xnu' / patch)], check=True)

    def test_headers_stage_before_patch_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            (tree / 'bsd/conf').mkdir(parents=True)
            (tree / 'bsd/dev').mkdir()
            (tree / 'bsd/conf/files').write_text('base\n')
            subprocess.run(['bash', str(ROOT / 'xnu/0087-stage-cepheus-gpu-memory.sh')], cwd=tree, check=True)
            for where, name in [('bsd/dev', 'cepheus_gpu_compile.c'), ('bsd/dev', 'cepheus_hfi_boot.h'),
                                ('bsd/dev', 'cepheus_gpu_power_mmio.h'),
                                ('bsd/dev', 'cepheus_gpu_power_native.h'),
                                ('bsd/dev', 'cepheus_gpu_power_native_private.h'),
                                ('bsd/dev', 'cepheus_gpu_tsens_native.h'),
                                ('bsd/dev', 'cepheus_gpu_tsens_native_private.h'),
                                ('bsd/dev', 'cepheus_gmu_start_native.h'),
                                ('bsd/dev', 'cepheus_gmu_start_native_private.h'),
                                ('bsd/dev', 'cepheus_gmu_oob_native.h'),
                                ('bsd/dev', 'cepheus_gmu_oob_native_private.h'),
                                ('bsd/dev', 'cepheus_gpu_pdc_native.h'),
                                ('bsd/dev', 'cepheus_gpu_pdc_native_private.h'),
                                ('bsd/dev', 'cepheus_smmu_native.h'),
                                ('bsd/dev', 'cepheus_smmu_native_private.h'),
                                ('bsd/dev', 'cepheus_gx_smmu_install.h'),
                                ('bsd/dev', 'cepheus_gx_smmu_native.h'),
                                ('bsd/dev', 'cepheus_gx_smmu_native_private.h'),
                                ('bsd/dev', 'cepheus_cp_native.h'),
                                ('bsd/dev', 'cepheus_cp_native_private.h'),
                                ('bsd/dev', 'cepheus_cp_ring_native.h'),
                                ('bsd/dev', 'cepheus_cp_ring_native_private.h'),
                                ('bsd/dev', 'cepheus_cp_execution_admission.h'),
                                ('bsd/dev', 'cepheus_cp_execution_admission_private.h'),
                                ('bsd/dev', 'cepheus_owner_evidence.h'),
                                ('bsd/dev', 'cepheus_resource_owner_provider.h'),
                                ('bsd/dev', 'cepheus_resource_owner_cp_admission.h'),
                                ('bsd/dev', 'cepheus_zap_resource_evidence.h'),
                                ('iokit/Kernel/arm', 'cepheus_cp_memory_api.h'),
                                ('iokit/Kernel/arm', 'cepheus_cp_memory_private.h'),
                                ('iokit/Kernel/arm', 'cepheus_cp_workspace.h'),
                                ('iokit/Kernel/arm', 'cepheus_gpu_vm39.h'),
                                ('iokit/Kernel/arm', 'cepheus_gx_smmu_plan.h'),
                                ('bsd/dev', 'cepheus_gmu_upload_native.h'),
                                ('bsd/dev', 'cepheus_gmu_upload_native_private.h'),
                                ('iokit/Kernel/arm', 'cepheus_gmu_upload.h'),
                                ('iokit/Kernel/arm', 'cepheus_gmu_firmware.h'),
                                ('iokit/Kernel/arm', 'cepheus_gpu_va.h'),
                                ('iokit/Kernel/arm', 'cepheus_gpu_vm.h'),
                                ('iokit/Kernel/arm', 'cepheus_gmu_start.h'),
                                ('bsd/dev', 'cepheus_gpll0.h'),
                                ('bsd/dev', 'cepheus_gpll0_private.h'),
                                ('bsd/dev', 'cepheus_clock_measure.h'),
                                ('bsd/dev', 'cepheus_clock_measure_private.h'),
                                ('bsd/dev', 'cepheus_qmp_native.h'),
                                ('bsd/dev', 'cepheus_qmp_native_private.h'),
                                ('bsd/dev', 'cepheus_bcm_active.h'),
                                ('bsd/dev', 'cepheus_bcm_active_private.h'),
                                ('bsd/dev', 'cepheus_rpmh_active_private.h'),
                                ('bsd/dev', 'cepheus_zap_image.h'),
                                ('bsd/dev', 'cepheus_zap_memory.h'),
                                ('bsd/dev', 'cepheus_zap_pas.h'),
                                ('bsd/dev', 'cepheus_scm_handoff.h'),
                                ('bsd/dev', 'cepheus_zap_memory_private.h'),
                                ('iokit/Kernel/arm', 'cepheus_zap_image.h'),
                                ('iokit/Kernel/arm', 'cepheus_zap_memory.h'),
                                ('iokit/Kernel/arm', 'cepheus_zap_memory_private.h'),
                                ('bsd/dev', 'cepheus_smmu_install.h'), ('iokit/Kernel/arm', 'cepheus_gpu_memory.cpp')]:
                self.assertEqual((tree / where / name).read_bytes(), (ROOT / 'xnu' / name).read_bytes())
            self.assertFalse((tree / 'bsd/dev/sweet_rpmh.c').exists())
            # Compile using only the staged IOKit directory and host platform
            # headers, so unstaged siblings cannot satisfy CP includes.
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',
                            '-DCEPHEUS_GPU_MEMORY_HOST_TEST=1',
                            '-I',str(ROOT/'tests/gpu-memory-host'),
                            '-I',str(tree/'iokit/Kernel/arm'),
                            '-c',str(tree/'iokit/Kernel/arm/cepheus_gpu_memory.cpp'),
                            '-o',str(tree/'memory.o')],check=True)

    def test_touch_system_info_header_staged(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            (tree / 'bsd/conf').mkdir(parents=True)
            (tree / 'bsd/dev').mkdir()
            (tree / 'bsd/conf/files').write_text('base\n')
            subprocess.run(['bash', str(ROOT / 'xnu/0082-stage-cepheus-touch.sh')], cwd=tree, check=True)
            for name in ('cepheus_touch.c', 'cepheus_fts_events.h', 'cepheus_fts_sysinfo.h'):
                self.assertEqual((tree / 'bsd/dev' / name).read_bytes(), (ROOT / 'xnu' / name).read_bytes())

    def test_patch_places_each_source_once(self):
        with tempfile.TemporaryDirectory() as directory:
            tree = Path(directory)
            subprocess.run(['git', 'init', '-q', str(tree)], check=True)
            for name, text in {
                'bsd/conf/files': 'bsd/dev/sweet_sample.c\t\t\tstandard\nbsd/dev/sweet_keys.c\t\t\tstandard\n'
                    'bsd/dev/cepheus_touch.c\t\t\tstandard\nbsd/dev/cepheus_soc.c\t\t\tstandard\n'
                    'bsd/dev/sweet_rpmh.c\t\t\tstandard\nbsd/dev/sweet_spmi.c\t\t\tstandard\n',
                'iokit/conf/files.arm64': 'iokit/Kernel/arm/SweetWatchdog.cpp                               optional iokitcpp\n'
                    'iokit/Kernel/arm/SweetSurface.cpp                                optional iokitcpp\n'
                    'iokit/Kernel/arm/SweetFramebuffer.cpp                            optional iokitcpp\n'
                    'iokit/Kernel/arm/SweetBattery.cpp                                optional iokitcpp\n'
                    'iokit/Kernel/arm/hid/SweetHID.cpp                                optional iokitcpp\n',
            }.items():
                path = tree / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            subprocess.run(['git', '-C', str(tree), 'apply', str(ROOT / 'xnu/0087-cepheus-gpu-memory.patch')], check=True)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_gpu_compile.c'), 1)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_gmu_workspace.c'), 1)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_gpu_retire.c'), 1)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_gmu_hfi_link.c'), 1)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_gmu_bringup.c'), 1)
            self.assertEqual((tree / 'bsd/conf/files').read_text().count('cepheus_cp_workspace.c'), 1)
            self.assertEqual((tree / 'iokit/conf/files.arm64').read_text().count('cepheus_gpu_memory.cpp'), 1)

    def test_memory_uses_explicit_nc_mapping(self):
        source = (ROOT / 'xnu/cepheus_gpu_memory.cpp').read_text()
        self.assertIn('createMappingInTask(kernel_task,0,', source)
        self.assertIn('kIOMapAnywhere|kIOMapUnique|policy->cache', source)
        self.assertIn('b->mapping->getVirtualAddress()', source)
        self.assertNotIn('b->memory->getBytesNoCopy()', source)
        self.assertIn('IODefaultCacheBits(b->pa+offset)&VM_WIMG_MASK', source)
        self.assertIn('if(attrs!=policy->wimg)', source)
        self.assertIn('kIOMapWriteCombineCache,VM_WIMG_WCOMB,0x44,false', source)
        self.assertIn('kIOMapRealTimeCache,VM_WIMG_RT,0,true', source)

    def test_pa_validation_precedes_cache_transition(self):
        source = (ROOT / 'xnu/cepheus_gpu_memory.cpp').read_text()
        source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
        allocation = source.split('b->memory=IOBufferMemoryDescriptor::inTaskWithPhysicalMask', 1)[1].split(';', 1)[0]
        self.assertNotIn('kIOMapWriteCombineCache', allocation)
        self.assertNotIn('kIOMemoryPersistent', allocation)
        self.assertLess(source.index('cepheus_gpu_pa_aligned_window_mask('), source.index('inTaskWithPhysicalMask('))
        allocation_body=source.split('ceph_gpu_buffer_alloc_cpu(',1)[1]
        self.assertLess(allocation_body.index('cepheus_gpu_pa_range_managed('), allocation_body.index('createMappingInTask('))
        self.assertNotIn('0x000000fffffff000', source)

    def test_wrappers_compile_without_runtime_backend(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-c',
                            str(ROOT / 'xnu/cepheus_gpu_compile.c'), '-o', str(Path(directory) / 'gpu.o')], check=True)

    def test_boot_sources_do_not_invoke_gpu_engines(self):
        # A build can accidentally retain all symbols AND introduce a startup
        # call. Keep this separate check on actual boot-facing source bodies.
        dangerous=('cepheus_gpu_arc_raise','cepheus_gpu_bcm_raise','cepheus_gmu_bringup_start','ceph_power_start','cepheus_gpu_power_start',
                   'cepheus_gpu_rpmh_hold','cepheus_gpu_idle_hold',
                   'cepheus_gpu_power_start_native',
                   'cepheus_gpu_tsens_bind_native','cepheus_gpu_temperature_sample_native','cepheus_gpu_thermal_now_native',
                   'cepheus_gmu_start_cold_native',
                   'cepheus_gpu_oob_acquire_native','cepheus_gpu_oob_release_native',
                   'cepheus_gpu_pdc_install_native',
                   'cepheus_gpu_smmu_install_native',
                   'cepheus_gpu_gx_smmu_install_native','ceph_gx_smmu_install_context',
                   'cepheus_gmu_upload_native',
                   'cepheus_gmu_hfi_version_native',
                   'cepheus_gmu_hfi_boot_native','cepheus_gmu_hfi_dcvs_native','cepheus_gmu_hfi_start_native',
                   'cepheus_gpu_config_build_native',
                   'cepheus_gpu_qmp_acd_native',
                   'cepheus_gpu_gpll0_hold','cepheus_gpu_gpll0_check','ceph_gpll0_hold',
                   'cepheus_gpu_gpll0_measure','ceph_clock_measure_run',
                   'ceph_qmp_connect_cold','ceph_qmp_send_acd','ceph_gx_core_configure',
                   'cepheus_gpu_gx_core_configure','ceph_cp_ring_configure_halted',
                   'cepheus_gpu_gx_core_native','cepheus_gpu_gx_setup_native','cepheus_gpu_gx_setup_perf_native',
                   'cepheus_cp_workspace_seal','ceph_memory_cp_seal','ceph_memory_cp_acquire','ceph_memory_cp_publish',
                   'cepheus_gpu_cp_prepare_native','ceph_memory_cp_build_acquire','ceph_memory_cp_build_seal',
                   'cepheus_gpu_submit','ceph_smmu_install_context','cepheus_gpu_zap_snapshot_prepare',
                   'cepheus_zap_memory_prepare','cepheus_gpu_zap_load','cepheus_gpu_zap_stop')
        dangerous += ('cepheus_gpu_owner_evidence_admit',)
        dangerous += ('cepheus_resource_owner_acquire',)
        dangerous += ('cepheus_resource_owner_cp_binding_init',)
        for name in ('cepheus_soc.c','cepheus_touch.c','cepheus_arc_probe.h'):
            text=(ROOT/'xnu'/name).read_text()
            text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
            for call in dangerous:self.assertNotRegex(text,rf'\b{call}\s*\(',name)
            if name != 'cepheus_soc.c':
                self.assertNotRegex(text,r'\bcepheus_gpu_scm_probe_once\s*\(',name)
        # Both diagnostic paths have explicit, mutually exclusive opt-ins.
        soc=(ROOT/'xnu/cepheus_soc.c').read_text()
        self.assertEqual(soc.count('cepheus_gpu_scm_probe_once('),2)
        pair=soc.split('static bool soc_scm_pair(void)',1)[1].split('static bool soc_zap_memory',1)[0]
        self.assertIn('ceph_scm_pair_profile(PE_boot_args())',pair)
        self.assertLess(pair.index('ceph_scm_handoff_valid('),pair.index('cepheus_gpu_scm_probe_once('))
        helper=soc.split('static void soc_scm_probe(void)',1)[1].split('static void soc_thread',1)[0]
        self.assertIn('cepheus_gpu_scm_probe_once(',helper)
        self.assertIn('if (enabled != 1) { return; }',helper)
        source=(ROOT/'xnu/cepheus_gpu_compile.c').read_text()
        body=source.split('int cepheus_gpu_config_probe',1)[1].split('int cepheus_gpu_qmp_connect',1)[0]
        for call in dangerous:self.assertNotRegex(body,rf'\b{call}\s*\(')
        self.assertNotRegex(body,r'\bcepheus_gpu_scm_probe_once\s*\(')

    def test_no_security_bypass_in_concrete_cp_adapters(self):
        for name in ('cepheus_cp_ring_config.h','cepheus_cp_protection.h',
                     'cepheus_cp_snapshot_mmio.h','cepheus_gx_core.h'):
            source=(ROOT/'xnu'/name).read_text()
            # Comments explicitly name forbidden registers; check executable
            # code, not the presence of those cautionary words.
            source=re.sub(r'/\*.*?\*/|//[^\n]*','',source,flags=re.S)
            self.assertNotRegex(source,r'gpu\[0x(?:808|807)\]\s*=(?!=)',name)
            self.assertNotIn('TRUST_CONTROL',source)


if __name__ == '__main__':
    unittest.main()
