#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for name in gmu_firmware gmu_upload hfi hfi_messages hfi_session gpu_vm gpu_va arc bcm gpu_bw gpu_ring cp_init smmu_plan gpu_mutations; do
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer \
        "$root/xnu/test_${name}.c" -o "$tmp/$name"
    "$tmp/$name"
done
