#!/usr/bin/env python3
"""Prepare/verify the restore-time files required by the SWEET iOS root.

Matches dipa-studio/ios-sweet tools/mkiosroot.sh for the no-passcode
AppleKeyStore shim. This is NOT a real-device keybag or SEP implementation.
Run as root against a mounted image, never the host filesystem.
"""
import argparse
import json
import os
from pathlib import Path
import plistlib
import stat


STORES = {"diagnostics": 0o751, "uuidtext": 0o755,
          "diagnostics/timesync": 0o755}
KEYBAG = "private/var/keybags/systembag.kb"
CACHE = "System/Library/Caches/com.apple.dyld"


def metadata(path, uid, gid, mode):
    s = path.lstat()
    actual = (s.st_uid, s.st_gid, stat.S_IMODE(s.st_mode))
    expected = (uid, gid, mode)
    if actual != expected:
        raise ValueError(f"{path}: owner/mode {actual}, expected {expected}")


def prepare(root):
    if os.geteuid() != 0:
        raise ValueError("preparation requires root to set numeric iOS owners")
    bag = root / KEYBAG
    bag.parent.mkdir(parents=True, exist_ok=True)
    os.chown(bag.parent, 0, 0)
    bag.parent.chmod(0o700)
    # Do not destroy an existing keybag on a previously booted image.
    if not bag.exists():
        with bag.open("xb") as f:
            plistlib.dump({"KeyBagKeys": b"SWEET"}, f, fmt=plistlib.FMT_XML)
    os.chown(bag, 0, 0)
    bag.chmod(0o600)
    for name, mode in STORES.items():
        directory = root / "private/var/db" / name
        directory.mkdir(parents=True, exist_ok=True)
        os.chown(directory, 272, 272)
        directory.chmod(mode)
        marker = directory / ".migrated"
        marker.touch(exist_ok=True)
        os.chown(marker, 272, 272)
        marker.chmod(0o644)


def verify(root):
    bag = root / KEYBAG
    metadata(bag.parent, 0, 0, 0o700)
    metadata(bag, 0, 0, 0o600)
    with bag.open("rb") as f:
        data = plistlib.load(f)
    if data != {"KeyBagKeys": b"SWEET"}:
        raise ValueError("systembag.kb does not match the SWEET no-passcode shim")
    for name, mode in STORES.items():
        directory = root / "private/var/db" / name
        metadata(directory, 272, 272, mode)
        metadata(directory / ".migrated", 272, 272, 0o644)
        if (directory / ".migrated").stat().st_size != 0:
            raise ValueError(f"{directory}: .migrated is not empty")

    cache = root / CACHE
    files = sorted(cache.iterdir())
    if len(files) != 44:
        raise ValueError(f"expected 44 cache files for 20H392, got {len(files)}")
    total = 0
    for path in files:
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"empty/missing cache file: {path}")
        with path.open("rb") as f:
            header = f.read(16)
        # Symbols files have their own header, not a dyld_v1 cache header.
        if not path.name.endswith(".symbols") and not header.startswith(b"dyld_v1"):
            raise ValueError(f"invalid dyld cache header: {path}: {header!r}")
        total += path.stat().st_size
    if total < 1 << 30:
        raise ValueError(f"cache too small: {total} bytes")
    critical = ("sbin/launchd", "usr/lib/dyld",
                "System/Library/CoreServices/SpringBoard.app/SpringBoard")
    for name in critical:
        if (root / name).stat().st_size == 0:
            raise ValueError(f"empty executable: {name}")
    return {"keybag": "SWEET shim plist; root:root 0600",
            "logd_markers": list(STORES), "cache_files": len(files),
            "cache_bytes": total,
            "executables": {p: (root / p).stat().st_size for p in critical}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "verify"))
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    if root == Path("/") or not (root / "System/Library/xpc/launchd.plist").is_file():
        parser.error("not a mounted iOS root (refusing to touch the host)")
    if args.action == "prepare":
        prepare(root)
    else:
        print(json.dumps(verify(root), indent=2))


if __name__ == "__main__":
    main()
