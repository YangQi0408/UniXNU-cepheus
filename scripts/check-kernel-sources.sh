#!/usr/bin/env bash
# Shared host checks for the SWEET kernel. No device or network operations.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PATCHES=${1:?usage: check-kernel-sources.sh PINNED_PATCHES}
cd "$ROOT"
python3 scripts/test-performance.py
python3 scripts/test-touch-power.py "$PATCHES"
for name in touch-fifo touch-replay touch-soc-log touch-sysinfo ready-boot-args \
    scm-hook scm-pair-hook scm-pair-log scm-query-log soc-delay soc-delay-packaging \
    gpu-integration pmap-cache-pa pmap-cache-verifier tree gpu-fdt \
    gpu-diagnostic-log gx-probe zap-layout zap-preflight-packaging \
    zap-cache-codegen zap-memory-log scm-codegen; do
    python3 "scripts/test-$name.py"
done
python3 scripts/test-gpu-arc.py "$PATCHES"
python3 scripts/test-arc-active-native.py "$PATCHES"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=undefined \
    xnu/test_scm_handoff.c -o "$tmp/handoff"
"$tmp/handoff"
for name in gpu-memory-host zap-memory-host zap-pas-host gpu-sanitizers gpu-stack; do
    bash "scripts/test-$name.sh"
done
python3 scripts/analyze-gpu-host.py
