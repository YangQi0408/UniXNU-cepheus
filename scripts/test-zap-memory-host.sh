#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
sanitizers=${CEPHEUS_GPU_SANITIZERS:-undefined}
libs=()
if [[ $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
for unit in cepheus_gpu_retire cepheus_gmu_workspace; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize="$sanitizers" -I"$root/xnu" \
        -c "$root/xnu/$unit.c" -o "$tmp/$unit.o"
done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize="$sanitizers" -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" -I"$root/xnu" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_zap_preflight.cpp" \
    "$tmp/cepheus_gpu_retire.o" -o "$tmp/zap-preflight" "${libs[@]}"
for mode in 0 1 2 3 4 5 6 7 8 9 10; do "$tmp/zap-preflight" "$mode"; done
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize="$sanitizers" -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" -I"$root/xnu" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_zap_memory.cpp" \
    "$tmp/cepheus_gpu_retire.o" "$tmp/cepheus_gmu_workspace.o" -o "$tmp/zap-memory" "${libs[@]}"
if [[ -n ${CEPHEUS_ZAP_ELF:-} ]]; then
    for mode in 0 1 2 3 4; do "$tmp/zap-memory" "$CEPHEUS_ZAP_ELF" "$mode"; done
else
    "$tmp/zap-memory"
fi
