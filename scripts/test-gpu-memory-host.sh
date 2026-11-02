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
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/xnu" \
    -c "$root/xnu/cepheus_gmu_workspace.c" -o "$tmp/workspace.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=undefined -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" -I"$root/xnu" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_gmu_workspace.cpp" "$tmp/workspace.o" \
    -o "$tmp/workspace" "${libs[@]}"
"$tmp/workspace"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=undefined -pthread \
    -DCEPHEUS_GPU_MEMORY_HOST_TEST=1 -I"$root/tests/gpu-memory-host" -I"$root/xnu" \
    "$root/xnu/cepheus_gpu_memory.cpp" "$root/tests/test_gmu_workspace_upload.cpp" "$tmp/workspace.o" \
    -o "$tmp/upload" "${libs[@]}"
if [[ $# -ge 1 ]]; then "$tmp/upload" "$1"; else "$tmp/upload"; fi
