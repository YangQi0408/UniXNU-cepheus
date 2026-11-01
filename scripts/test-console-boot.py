#!/usr/bin/env python3
"""Verify a quiet control image against its original; requires mtools.

Usage: test-console-boot.py ORIGINAL.img QUIET.img
"""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def main():
    original, quiet = (Path(p).read_bytes() for p in sys.argv[1:3])
    assert len(original) == len(quiet)
    k, _, r, _, s, _, _, page = struct.unpack_from("<8I", original, 8)
    off = page + ((k + page - 1) // page) * page
    assert original[:576] == quiet[:576]
    assert original[584:off] == quiet[584:off]
    assert original[off + r:] == quiet[off + r:]
    assert s == 0
    digest = hashlib.sha1(quiet[page:page + k] + quiet[off:off + r]
                          + struct.pack("<III", k, r, s)).digest()[:8]
    assert quiet[576:584] == digest
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        configs = []
        members = ("kernel.macho", "kernel.macho.adt")
        hashes = []
        for i, boot in enumerate((original, quiet)):
            fat = root / f"payload-{i}.img"
            fat.write_bytes(boot[off:off + r])
            extracted = []
            for member in members:
                target = root / f"{i}-{member}"
                subprocess.run(["mcopy", "-i", str(fat),
                                f"::/EFI/UniXNU/Payloads/{member}", str(target)], check=True)
                extracted.append(hashlib.sha256(target.read_bytes()).hexdigest())
            hashes.append(extracted)
            conf = root / f"{i}.toml"
            subprocess.run(["mcopy", "-i", str(fat), "::/EFI/UniXNU/unixnu.toml", str(conf)], check=True)
            configs.append(conf.read_text())
        assert hashes[0] == hashes[1]
        assert configs[0].count("sweetfb_noconsole=0") == 1
        assert configs[1] == configs[0].replace("sweetfb_noconsole=0", "sweetfb_noconsole=1")
    print("PASS: kernel/ADT identical; only console argument changed; firmware/DTB/header layout preserved")


if __name__ == "__main__":
    main()
