#!/usr/bin/env python3
"""Replace only the XNU payload in a proven quiet Aloha boot image.

Retains the original ADT, UEFI, DTB, Android header layout and image sizes.
Enables delayed cepheus touch; keys remain off. Never flashes any partition.
Usage: make-touch-boot.py QUIET.img KERNEL.macho OUT.img [--delay 60]
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import tomllib


def mcopy(fat, source, target, overwrite=False):
    subprocess.run(["mcopy", *(["-o"] if overwrite else []), "-i", str(fat),
                    str(source), str(target)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--delay", type=int, default=60)
    args = parser.parse_args()
    if args.output.exists() or not 45 <= args.delay <= 300:
        parser.error("output must be new; delay must be 45..300 seconds")
    subprocess.run(["python3", str(Path(__file__).with_name("verify-touch-kernel.py")),
                    str(args.kernel)], check=True)
    original = args.source.read_bytes()
    if original[:8] != b"ANDROID!":
        parser.error("not an Android boot image")
    k, _, r, _, second, _, _, page = struct.unpack_from("<8I", original, 8)
    if page != 4096 or second or struct.unpack_from("<I", original, 40)[0]:
        parser.error("expected this port's Aloha v0 layout")
    off = page + ((k + page - 1) // page) * page
    sizes = struct.pack("<III", k, r, second)
    old_fat = original[off:off + r]
    kernel_region = original[page:page + k]
    if len(old_fat) != r or original[576:584] != hashlib.sha1(
            kernel_region + old_fat + sizes).digest()[:8]:
        parser.error("unexpected Aloha image size/id")
    kernel_member = "::/EFI/UniXNU/Payloads/kernel.macho"
    adt_member = kernel_member + ".adt"
    config_member = "::/EFI/UniXNU/unixnu.toml"
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        fat = work / "payload.img"
        fat.write_bytes(old_fat)
        adt_before = work / "adt-before"
        config = work / "unixnu.toml"
        mcopy(fat, adt_member, adt_before)
        mcopy(fat, config_member, config)
        text = config.read_text()
        line = tomllib.loads(text)["entry"][0]["args"]
        tokens = line.split()
        for required in ("sweetfb_noconsole=1", "sweettouch=0", "sweetkeys=0", "sweetusb=2"):
            if tokens.count(required) != 1:
                parser.error(f"source must have exactly one {required}")
        tokens = ["sweettouch=1" if t == "sweettouch=0" else t for t in tokens
                  if not t.startswith("sweettouchdelay=")]
        tokens.append(f"sweettouchdelay={args.delay}")
        new_line = " ".join(tokens)
        if len(new_line) > 1000:
            parser.error("boot args too long")
        new, count = re.subn(r'^args = "[^"\n]*"$', f'args = "{new_line}"', text, flags=re.M)
        if count != 1:
            parser.error("expected one entry args line")
        config.write_text(new)
        mcopy(fat, args.kernel, kernel_member, overwrite=True)
        mcopy(fat, config, config_member, overwrite=True)
        adt_after = work / "adt-after"
        kernel_after = work / "kernel-after"
        config_after = work / "config-after"
        mcopy(fat, adt_member, adt_after)
        mcopy(fat, kernel_member, kernel_after)
        mcopy(fat, config_member, config_after)
        assert adt_after.read_bytes() == adt_before.read_bytes(), "ADT changed"
        assert kernel_after.read_bytes() == args.kernel.read_bytes(), "kernel copy mismatch"
        assert config_after.read_text() == new, "config copy mismatch"
        payload = fat.read_bytes()
        assert len(payload) == r
    boot = bytearray(original)
    boot[off:off + r] = payload
    boot[576:584] = hashlib.sha1(kernel_region + payload + sizes).digest()[:8]
    assert boot[:576] == original[:576]
    assert boot[584:off] == original[584:off]
    assert boot[off + r:] == original[off + r:]
    with args.output.open("xb") as f:
        f.write(boot)
    print(new_line)
    print("verified: exact new kernel; ADT/UEFI/DTB unchanged; Aloha id updated")
    print(hashlib.sha256(boot).hexdigest(), args.output)


if __name__ == "__main__":
    main()
