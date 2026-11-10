#!/usr/bin/env bash
# Focused mixed native/pool chain. Explicit original firmware; no phone access.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
firmware=${1:?supply explicit original a640_gmu.bin}
out=${2:?supply fresh output directory}
test ! -e "$out"
mkdir -p "$out"
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS=detect_leaks=0
common=(-Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -I"$root/xnu")
for unit in cepheus_gpu_compile.c cepheus_gmu_hfi_link.c cepheus_gmu_workspace.c cepheus_gpu_retire.c test_gmu_hfi_native_pool.c; do
    clang -std=c11 "${common[@]}" -c "$root/xnu/$unit" -o "$out/$unit.o"
done
libs=()
if [[ $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
clang++ -std=c++17 "${common[@]}" -pthread -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 \
    -I"$root/tests/gpu-memory-host" "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_hfi_native_pool.cpp" \
    "$out/cepheus_gpu_compile.c.o" "$out/cepheus_gmu_hfi_link.c.o" "$out/cepheus_gmu_workspace.c.o" \
    "$out/cepheus_gpu_retire.c.o" "$out/test_gmu_hfi_native_pool.c.o" -o "$out/hfi-pool" "${libs[@]}"
for mode in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
    "$out/hfi-pool" "$firmware" "$mode" > "$out/mode-$mode.log" 2>&1
    if rg -n 'runtime error:|ERROR: AddressSanitizer|SUMMARY:.*Sanitizer' "$out/mode-$mode.log"; then exit 1; fi
    tail -n 1 "$out/mode-$mode.log"
done
