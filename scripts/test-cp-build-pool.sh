#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
sqe=${1:?supply original a630_sqe.fw}
out=${2:?supply fresh output directory}
test ! -e "$out"
mkdir -p "$out"
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
common=(-O1 -g -fno-omit-frame-pointer -Wall -Wextra -Werror -fsanitize=address,undefined -I"$root/xnu")
for unit in xnu/cepheus_cp_workspace.c xnu/cepheus_gmu_workspace.c xnu/cepheus_gpu_retire.c tests/test_cp_workspace_restore.c; do
    clang -std=c11 "${common[@]}" -c "$root/$unit" -o "$out/$(basename "$unit").o"
done
libs=()
if [[ $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
clang++ -std=c++17 "${common[@]}" -Wno-missing-field-initializers -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_cp_build_pool.cpp" "$out/"*.o \
    -o "$out/cp-build-pool" "${libs[@]}"
for mode in {0..9}; do
    "$out/cp-build-pool" "$sqe" "$mode" > "$out/mode-$mode.log" 2>&1
    if rg -n 'runtime error:|ERROR: AddressSanitizer|SUMMARY:.*Sanitizer' "$out/mode-$mode.log"; then exit 1; fi
    tail -n 1 "$out/mode-$mode.log"
done
