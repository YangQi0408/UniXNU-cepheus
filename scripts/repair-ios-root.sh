#!/usr/bin/env bash
# Copy a local RAW HFSX image and add missing restore-time files.
# Never flashes the phone; never edits the source image.
# Run in root-tools with /dev exposed for loop mounts.
set -euo pipefail
SOURCE=$(realpath "${1:?usage: repair-ios-root.sh SOURCE.img OUT.img}")
OUT=$(realpath -m "${2:?usage: repair-ios-root.sh SOURCE.img OUT.img}")
[[ $OUT == *.img ]] || { echo 'output must end in .img' >&2; exit 1; }
[[ ! -e $OUT && ! -e ${OUT%.img}.simg ]] || { echo 'refusing to overwrite output' >&2; exit 1; }
[[ $(id -u) == 0 ]] || { echo 'run as root inside root-tools' >&2; exit 1; }
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d)
cleanup() {
    if mountpoint -q "$work"; then
        umount "$work" || { echo "left mounted at $work; not deleting" >&2; return; }
    fi
    rmdir "$work"
}
trap cleanup EXIT
cp --reflink=auto --sparse=always "$SOURCE" "$OUT"
mount -t hfsplus -o loop,force "$OUT" "$work"
python3 "$here/prepare-ios-root.py" prepare "$work"
python3 "$here/prepare-ios-root.py" verify "$work"
umount "$work"
fsck.hfsplus -f -n "$OUT"
mount -t hfsplus -o loop,ro "$OUT" "$work"
python3 "$here/prepare-ios-root.py" verify "$work"
umount "$work"
img2simg "$OUT" "${OUT%.img}.simg"
echo "DONE (offline verified; not yet boot tested): ${OUT%.img}.simg"
