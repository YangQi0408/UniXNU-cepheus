#!/usr/bin/env bash
# Actual CP/pool publication with explicit original SQE; no hardware access.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
firmware=${1:?supply explicit original a630_sqe.fw}
out=${2:?supply fresh output directory}
test ! -e "$out"
mkdir -p "$out"
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
common=(-O1 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -fsanitize=address,undefined -I"$root/xnu")
for unit in xnu/cepheus_gpu_compile.c xnu/cepheus_gmu_workspace.c xnu/cepheus_gpu_retire.c \
    xnu/cepheus_cp_workspace.c tests/test_cp_workspace_restore.c xnu/test_cp_preinit.c; do
    clang -std=c11 "${common[@]}" -c "$root/$unit" -o "$out/$(basename "$unit").o"
done
libs=()
if [[ $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
clang++ -std=c++17 "${common[@]}" -Wno-missing-field-initializers -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_cp_workspace.cpp" "$out/"*.o \
    -o "$out/cp-pool" "${libs[@]}"
for mode in {0..11}; do
    "$out/cp-pool" "$firmware" "$mode" > "$out/mode-$mode.log" 2>&1
    if rg -n 'runtime error:|ERROR: AddressSanitizer|SUMMARY:.*Sanitizer' "$out/mode-$mode.log"; then exit 1; fi
    tail -n 1 "$out/mode-$mode.log"
done
