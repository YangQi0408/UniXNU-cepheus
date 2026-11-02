#!/usr/bin/env bash
# Host-only deterministic regressions; never opens a device or submits MMIO.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for name in fts_events i2c_bitbang soc cpu_policy a640_pm4 gmu_firmware \
    gpu_power gpu_power_txn gpu_memory_state gpu_firmware gpu_fault gpu_pdc gpu_thermal gmu_loader gmu_upload gmu_start gmu_park gmu_slumber hfi hfi_messages hfi_session gpu_vm gpu_va smmu_plan smmu_geometry smmu_install smmu_detach gpu_submit gpu_ring cp_init qmp_acd sqe arc bcm gpu_bw; do
    libs=()
    if [[ "$name" == gpu_firmware && $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
        "$root/xnu/test_${name}.c" -o "$tmp/$name" "${libs[@]}"
    "$tmp/$name"
done
echo '35 host test programs passed; no hardware backend or GPU execution validated'
