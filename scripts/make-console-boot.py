#!/usr/bin/env python3
"""Make a one-argument control image from a proven Android v0 boot image.

Changes only sweetfb_noconsole in EFI/UniXNU/unixnu.toml within its FAT
ramdisk. The kernel, ADT, UEFI, DTB, Android header and image size remain
unchanged. Requires mtools; refuses to overwrite an existing output.
Usage: make-console-boot.py BOOT.img OUT.img --quiet 1
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile


MEMBERS = ("::/EFI/UniXNU/Payloads/kernel.macho",
           "::/EFI/UniXNU/Payloads/kernel.macho.adt")
CONFIG = "::/EFI/UniXNU/unixnu.toml"


def extract(image, member, output):
    subprocess.run(["mcopy", "-i", str(image), member, str(output)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--quiet", type=int, choices=(0, 1), required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("refusing to overwrite output")
    boot = bytearray(args.source.read_bytes())
    if boot[:8] != b"ANDROID!":
        parser.error("expected Android boot image v0")
    kernel_size, _, ramdisk_size, _, _, _, _, page = struct.unpack_from("<8I", boot, 8)
    if page != 4096 or struct.unpack_from("<I", boot, 40)[0] != 0:
        parser.error("unsupported page size or header version")
    offset = page + ((kernel_size + page - 1) // page) * page
    if offset + ramdisk_size > len(boot):
        parser.error("truncated ramdisk")
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        fat = work / "payload.img"
        fat.write_bytes(boot[offset:offset + ramdisk_size])
        before = []
        for i, member in enumerate(MEMBERS):
            path = work / f"before-{i}"
            extract(fat, member, path)
            before.append(path.read_bytes())
        config = work / "unixnu.toml"
        extract(fat, CONFIG, config)
        text = config.read_text()
        new, count = re.subn(r"\bsweetfb_noconsole=[01]\b",
                             f"sweetfb_noconsole={args.quiet}", text)
        if count != 1 or text == new:
            parser.error("expected exactly one changed sweetfb_noconsole argument")
        config.write_text(new)
        subprocess.run(["mcopy", "-o", "-i", str(fat), str(config), CONFIG], check=True)
        verify = work / "verify.toml"
        extract(fat, CONFIG, verify)
        assert verify.read_text() == new
        for i, member in enumerate(MEMBERS):
            path = work / f"after-{i}"
            extract(fat, member, path)
            assert path.read_bytes() == before[i], member
        payload = fat.read_bytes()
        assert len(payload) == ramdisk_size
        boot[offset:offset + ramdisk_size] = payload
    # This Aloha tree's mkbootimg.py uses an 8-byte digest followed by the
    # extended command line at 584, not Android's usual 32-byte id. Preserve
    # the input packer's layout; never overwrite extended command-line bytes.
    second_size = struct.unpack_from("<I", boot, 24)[0]
    dt_size = struct.unpack_from("<I", boot, 40)[0]
    second_off = offset + ((ramdisk_size + page - 1) // page) * page
    kernel = boot[page:page + kernel_size]
    second = boot[second_off:second_off + second_size]
    sizes = struct.pack("<III", kernel_size, ramdisk_size, second_size)
    original = args.source.read_bytes()
    original_payload = original[offset:offset + ramdisk_size]
    aloha_id = hashlib.sha1(kernel + original_payload + second + sizes).digest()[:8]
    if original[576:584] == aloha_id:
        boot[576:584] = hashlib.sha1(kernel + payload + second + sizes).digest()[:8]
        print("preserved Aloha 8-byte boot id layout")
    else:
        def android_id(ramdisk):
            digest = hashlib.sha1()
            for data in (kernel, ramdisk, second):
                digest.update(data)
                digest.update(struct.pack("<I", len(data)))
            if dt_size:
                dt_off = second_off + ((second_size + page - 1) // page) * page
                digest.update(boot[dt_off:dt_off + dt_size])
                digest.update(struct.pack("<I", dt_size))
            return digest.digest() + bytes(12)
        if original[576:608] != android_id(original_payload):
            parser.error("unknown boot id layout (refusing to change the header)")
        boot[576:608] = android_id(payload)
    with args.output.open("xb") as f:
        f.write(boot)
    print(new.strip())
    print(f"kernel and ADT unchanged; output {len(boot)} bytes")
    print(hashlib.sha256(boot).hexdigest(), args.output)


if __name__ == "__main__":
    main()
