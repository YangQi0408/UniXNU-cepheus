#!/usr/bin/env bash
# Copy the cepheus XNU patches into ios-sweet's patch directory.
#
# ios-sweet applies xnu/patches/*.patch in filename order with `git apply`.
# Stage the cepheus source before the patch loop; 0082 changes only the
# compiled file in bsd/conf/files after upstream reference-driver patches.
# Do not replace the reference source in place: later 0062/0063 patches target
# it, and a malformed/stale patch can be silently skipped by build.sh.
#
# Usage: build-kernel-patches.sh /path/to/ios-sweet
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:?usage: build-kernel-patches.sh /path/to/ios-sweet}"
[ -d "$DEST/xnu/patches" ] || { echo "ERROR: $DEST/xnu/patches not found" >&2; exit 1; }

install -m 644 "$ROOT/xnu/0082-cepheus-touch.patch" "$DEST/xnu/patches/0082-cepheus-touch.patch"
install -m 644 "$ROOT/xnu/0082-stage-cepheus-touch.sh" "$DEST/xnu/patches/0082-stage-cepheus-touch.sh"
install -m 644 "$ROOT/xnu/cepheus_touch.c" "$ROOT/xnu/cepheus_fts_events.h" "$DEST/xnu/patches/"
install -m 644 "$ROOT/xnu/cepheus_clock_status.h" "$DEST/xnu/patches/"
install -m 644 "$ROOT/xnu/cepheus_gpi_layout.h" "$DEST/xnu/patches/"
install -m 644 "$ROOT/xnu/cepheus_i2c_bitbang.h" "$DEST/xnu/patches/"
install -m 644 "$ROOT/xnu/0085-stage-cepheus-soc.sh" "$ROOT/xnu/0085-cepheus-soc.patch" "$ROOT/xnu/cepheus_soc.c" "$ROOT/xnu/cepheus_soc.h" "$DEST/xnu/patches/"
install -m 644 "$ROOT/xnu/0086-stage-cepheus-reboot.sh" "$ROOT/xnu/0086-cepheus-reboot.patch" "$ROOT/xnu/cepheus_reboot.c" "$ROOT/xnu/cepheus_reboot.h" "$DEST/xnu/patches/"
install -m 644 "$ROOT/xnu/cepheus_cpu_policy.h" "$DEST/xnu/patches/"
for required in 0082-cepheus-touch.patch 0085-cepheus-soc.patch 0086-cepheus-reboot.patch 0086-stage-cepheus-reboot.sh cepheus_reboot.c cepheus_reboot.h; do
    test -s "$DEST/xnu/patches/$required" || { echo "ERROR: missing staged $required" >&2; exit 1; }
done
echo "installed Cepheus touch, SoC, and refusal-gated reboot patches into $DEST/xnu/patches"
ls -l "$DEST/xnu/patches/" | tail -3
