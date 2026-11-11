#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
sanitizers=${CEPHEUS_GPU_SANITIZERS:-undefined}
libs=()
if [[ $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize="$sanitizers" -I"$root/xnu" \
    -c "$root/xnu/cepheus_gpu_retire.c" -o "$tmp/retire.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize="$sanitizers" -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" -I"$root/xnu" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_zap_pas.cpp" "$tmp/retire.o" \
    -o "$tmp/zap-pas" "${libs[@]}"
"$tmp/zap-pas"
if [[ -n ${CEPHEUS_ZAP_ELF:-} ]]; then
    for mode in {0..27}; do "$tmp/zap-pas" "$CEPHEUS_ZAP_ELF" "$mode"; done
fi
