#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for name in gmu_firmware zap_image gmu_upload hfi hfi_messages hfi_session gpu_vm gpu_vm39 gpu_va \
    arc arc_active bcm gpu_bw gpu_ring cp_init smmu_plan gpu_mutations qmp_acd qmp_mmio qmp_connect \
    gpu_tsens gpu_tsens_native gmu_start_native gpu_pdc_native smmu_native smmu_mmio gmu_mmio gmu_retire_mmio pdc_mmio gpu_fault_mmio gmu_upload_mmio gmu_upload_native cp_ring_config cp_protection cp_snapshot_mmio gx_core gpu_keepalive gx_smmu_plan gpu_power_txn gpu_power_mmio gpu_power_native gpll0 clock_measure bcm_active; do
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$root/xnu/test_${name}.c" -o "$tmp/$name"
    "$tmp/$name"
done
for name in gpu_idle gpu_idle_native; do
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -pthread -fsanitize=address,undefined \
        -fno-omit-frame-pointer "$root/xnu/test_${name}.c" -o "$tmp/$name"
    "$tmp/$name"
done
