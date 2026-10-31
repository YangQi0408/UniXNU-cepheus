#!/usr/bin/env python3
"""Pin the Aloha flash device to the size the cepheus memory map reserves.

`Device/xiaomi-cepheus/Library/PlatformMemoryMapLib/PlatformMemoryMapLib.c`
(and the matching `Binaries/RawFiles/uefiplat.cfg`) lay out the top of DRAM as:

    {"FV Region", 0x9F800000, 0x00400000}   0x9F800000..0x9FC00000
    {"UEFI FD",   0x9FC00000, 0x00300000}   0x9FC00000..0x9FF00000
    {"SEC Heap",  0x9FF00000, 0x0008C000}
    {"CPU Vectors", ...}  {"MMU PageTables", ...}  {"Info Blk", ...}

so the firmware volume MUST be 0x00300000 (3 MiB). Growing it to 4 MiB makes
it run to 0xA0000000 and overwrite SEC Heap, CPU Vectors, MMU page tables, UEFI
Stack and the Info Block: the firmware draws its screen and then hangs. That
was tried on this port and is exactly what it looked like.

Fitting in 3 MiB is a BUILD-TARGET question, not a trimming question:

  DEBUG   FVMAIN_COMPACT = 0x308610 .. 0x313490  -> GenFv: ERROR 3000
  RELEASE FVMAIN_COMPACT = 0x0c3fc18             -> fits with room to spare

RELEASE is what the raphael reference port ships and what its CI defaults to
(`target: RELEASE`). Keep every RAW resource: the 28 panel XMLs are ~370 KB
of the budget and dropping them is not needed once the target is RELEASE, so
this script only pins the size and leaves the device directory untouched.

Idempotent: re-running changes nothing.
"""
import re
import sys
from pathlib import Path

FD_SIZE = "0x00300000"
FD_BLOCKS = "0x300"


def pin_fdf(root):
    fdf = Path(root) / "Platforms/SurfaceDuo1Pkg/SurfaceDuo1.fdf"
    text = fdf.read_text()
    new = re.sub(r"Size\s*=\s*0x[0-9A-Fa-f]+\|gArmTokenSpaceGuid\.PcdFdSize",
                 f"Size          = {FD_SIZE}|gArmTokenSpaceGuid.PcdFdSize", text)
    new = re.sub(r"NumBlocks\s*=\s*0x[0-9A-Fa-f]+",
                 f"NumBlocks     = {FD_BLOCKS}", new)
    new = re.sub(r"^0x00000000\|0x[0-9A-Fa-f]+$", f"0x00000000|{FD_SIZE}",
                 new, flags=re.M)
    if new != text:
        fdf.write_text(new)
        print(f"fdf: pinned flash device to {FD_SIZE} / {FD_BLOCKS} blocks")
    else:
        print(f"fdf: already {FD_SIZE} / {FD_BLOCKS} blocks")


def main(argv):
    root = argv[1] if len(argv) > 1 else "."
    pin_fdf(root)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
