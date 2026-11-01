#!/usr/bin/env bash
# Pull the kernel's persistent console log out of the phone's userdata.
#
# Patch 0047 keeps a 4 MiB ring of the kernel console in the LAST 1025 blocks
# of the userdata partition (1 block of "SWEETLOG" header + 4 MiB of ring,
# 4096-byte UFS blocks). The kernel flushes it about once a second, so after a
# hang or a panic the last ~4 MiB of console survives a reboot.
#
# This runs against a booted RECOVERY (OrangeFox/TWRP) with adb root, because
# the phone's own Android is gone once userdata holds the iOS root.
#
# Usage: pull-log.sh [OUT]
set -euo pipefail

OUT="${1:-kernel-console.log}"
BLOCKS=1025

if ! adb shell true >/dev/null 2>&1; then
	echo "no adb device (boot recovery with: fastboot boot recovery.img)" >&2
	exit 1
fi

part=$(adb shell 'readlink -f /dev/block/by-name/userdata' | tr -d '\r')
[[ $part == /dev/block/* ]] || { echo 'invalid userdata path' >&2; exit 1; }
# Partitions live below their parent disk in /sys/block. /sys/class/block
# would also work; blockdev avoids relying on the sysfs hierarchy entirely.
size_bytes=$(adb shell "blockdev --getsize64 $part" | tr -d '\r')
[[ $size_bytes =~ ^[0-9]+$ ]] || { echo 'cannot read userdata size (root recovery required)' >&2; exit 1; }
(( size_bytes >= BLOCKS * 4096 && size_bytes % 4096 == 0 )) || { echo 'unexpected userdata size' >&2; exit 1; }
echo "userdata: $part, $size_bytes bytes" >&2

# 512-byte sectors -> the log area starts 1025 * 4096 bytes before the end.
skip=$(( (size_bytes - BLOCKS * 4096) / 512 ))
echo "reading $BLOCKS blocks from sector $skip" >&2

adb exec-out "dd if=$part bs=512 skip=$skip count=$((BLOCKS * 8))" > "$OUT.raw"
echo "wrote $OUT.raw ($(stat -c %s "$OUT.raw") bytes)" >&2

python3 - "$OUT.raw" "$OUT" <<'PY'
import struct, sys

raw, out = sys.argv[1], sys.argv[2]
data = open(raw, 'rb').read()
if len(data) != 1025 * 4096:
    sys.exit(f'truncated log area: {len(data)} bytes, expected {1025 * 4096}')
magic, version, size, total, seq = struct.unpack_from('<8sIIQQ', data, 0)
if magic != b'SWEETLOG':
    sys.exit(f'no SWEETLOG header at offset 0 (got {magic!r}); '
             'the log was never flushed or this is not the log area')
if version != 1 or size != 4 * 1024 * 1024:
    sys.exit(f'unsupported log header: version={version}, ring={size}')
ring = data[4096:4096 + size]
if total <= size:
    text = ring[:total]
else:
    start = total % size
    text = ring[start:] + ring[:start]
print(f'# version {version}, ring {size} B, total {total} B, flush #{seq}'
      f'{" (wrapped: oldest bytes lost)" if total > size else ""}',
      file=sys.stderr)
open(out, 'wb').write(text.replace(b'\r', b''))
print(f'wrote {out} ({len(text)} bytes)', file=sys.stderr)
PY
