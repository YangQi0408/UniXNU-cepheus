#!/usr/bin/env bash
# Copy the cepheus XNU patches into ios-sweet's patch directory.
#
# ios-sweet applies xnu/patches/*.patch in filename order with `git apply`.
# Its 0060-sweet-keys-touch-drivers.patch CREATES bsd/dev/sweet_touch.c (the
# sweet SE7 / Goodix GT9896 driver). Our 0082-cepheus-touch.patch sorts after
# it and replaces that file's contents with the cepheus SE17 / ST-FTS driver,
# so the later patch wins and no source edit to ios-sweet is needed.
#
# Usage: build-kernel-patches.sh /path/to/ios-sweet
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:?usage: build-kernel-patches.sh /path/to/ios-sweet}"
[ -d "$DEST/xnu/patches" ] || { echo "ERROR: $DEST/xnu/patches not found" >&2; exit 1; }

install -m 644 "$ROOT/xnu/0082-cepheus-touch.patch" "$DEST/xnu/patches/0082-cepheus-touch.patch"
echo "installed 0082-cepheus-touch.patch into $DEST/xnu/patches"
ls -l "$DEST/xnu/patches/" | tail -3
