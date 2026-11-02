#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
libs=()
if [[ $(uname -s) != Darwin ]]; then libs=(-lcrypto); fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" -I"$root/xnu" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_gpu_memory_host.cpp" \
    -o "$tmp/test" "${libs[@]}"
"$tmp/test" "$@"
