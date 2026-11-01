#!/usr/bin/env python3
"""Fail closed if a build artifact still has the reference touch driver."""
from pathlib import Path
import hashlib
import struct
import sys

path = Path(sys.argv[1])
data = path.read_bytes()
if len(data) < 2_000_000 or data[:4] != b"\xcf\xfa\xed\xfe":
    sys.exit("not a substantial arm64 Mach-O kernel")
if struct.unpack_from("<I", data, 4)[0] != 0x0100000c:
    sys.exit("expected CPU_TYPE_ARM64")
for marker in (b"SWEET TOUCH (cepheus): mapping GCC/TLMM/SE17",
               b"SWEET TOUCH (cepheus): selected ST-FTS",
               b"SWEET TOUCH (cepheus): active scan err="):
    if marker not in data:
        sys.exit(f"missing compiled touch marker: {marker!r}")
for marker in (b"SWEET TOUCH: mapping GCC/TLMM/SE7", b"GT9896 Yellowstone"):
    if marker in data:
        sys.exit(f"reference touch driver still present: {marker!r}")
print(f"cepheus driver compiled, reference driver absent: {path}")
print(hashlib.sha256(data).hexdigest())
