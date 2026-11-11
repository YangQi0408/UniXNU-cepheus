#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:+$UBSAN_OPTIONS:}halt_on_error=1:print_stacktrace=1"
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for name in gmu_firmware zap_image gmu_upload hfi hfi_access hfi_messages hfi_session gpu_vm gpu_vm39 gpu_va \
    arc arc_active bcm gpu_bw gpu_ring cp_init smmu_plan gpu_mutations qmp_acd qmp_mmio qmp_connect qmp_native \
    gpu_tsens gpu_tsens_native gmu_start_native gmu_oob_native gpu_pdc_native smmu_native smmu_mmio gmu_mmio gmu_retire_mmio pdc_mmio gpu_fault_mmio gmu_upload_mmio gmu_upload_native cp_ring_config cp_ring_native cp_protection cp_snapshot_mmio cp_native gx_core gx_core_native gx_smmu_native gpu_keepalive gx_smmu_plan gpu_power_txn gpu_power_mmio gpu_power_native gpll0 clock_measure bcm_active owner_evidence zap_resource_evidence resource_owner_provider resource_owner_cp_admission; do
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$root/xnu/test_${name}.c" -o "$tmp/$name"
    "$tmp/$name"
done
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -I"$root/xnu" -fsanitize=address,undefined \
    -fno-omit-frame-pointer "$root/xnu/test_gmu_hfi_native.c" "$root/xnu/cepheus_gmu_hfi_link.c" -o "$tmp/gmu-hfi-native"
"$tmp/gmu-hfi-native"
for name in gpu_idle gpu_idle_native; do
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
        -fno-omit-frame-pointer "$root/xnu/test_${name}.c" -o "$tmp/$name"
    "$tmp/$name"
done
