#!/usr/bin/env bash
# Host-only deterministic regressions; never opens a device or submits MMIO.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for name in fts_events i2c_bitbang soc cpu_policy a640_pm4 gmu_firmware gpu_config gpu_config_native hfi_access qmp_mmio qmp_native gmu_retire_mmio pdc_mmio gpu_fault_mmio gmu_upload_mmio gmu_upload_native qmp_connect gpu_tsens gpu_tsens_native cp_ring_config cp_ring_native cp_protection cp_snapshot_mmio cp_native gpu_vm39 gx_core gx_core_native gx_smmu_native gpu_keepalive gx_smmu_plan \
    gpu_power gpu_power_txn gpu_power_mmio gpu_power_native gpll0 clock_measure gpu_memory_state gpu_firmware zap_image gpu_fault gpu_pdc gpu_pdc_native gpu_thermal gmu_loader gmu_upload gmu_start gmu_start_native gmu_park gmu_slumber gmu_oob gmu_oob_native gmu_mmio hfi hfi_messages hfi_session gpu_vm gpu_va smmu_plan smmu_geometry smmu_mmio smmu_native smmu_install smmu_detach gpu_submit gpu_ring cp_init qmp_acd sqe arc arc_active bcm bcm_active gpu_bw gx_perfcounter gx_read_gate gx_va_layout gx_gbif gx_llc gx_hwcg cp_unsecure_gate gpu_pa_window scm_discovery fts_sysinfo owner_evidence zap_resource_evidence; do
    libs=()
    if [[ "$name" == gpu_firmware && $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
    "${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=undefined \
        "$root/xnu/test_${name}.c" -o "$tmp/$name" "${libs[@]}"
    "$tmp/$name"
done
"${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -O2 -I"$root/xnu" \
    "$root/xnu/test_gmu_hfi_native.c" "$root/xnu/cepheus_gmu_hfi_link.c" -o "$tmp/gmu-hfi-native"
"$tmp/gmu-hfi-native"
for name in gpu_idle gpu_idle_native; do
    "${CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -pthread -fsanitize=undefined \
        "$root/xnu/test_${name}.c" -o "$tmp/$name"
    "$tmp/$name"
done
echo '84 host test programs passed; no hardware backend or GPU execution validated'
