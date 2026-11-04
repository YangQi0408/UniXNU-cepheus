#!/usr/bin/env python3
"""Build the Aloha NOSB firmware volume with the UniXNU/DarwinBoot INFs for cepheus.

The Project Aloha tree already ships a first-class ``xiaomi-cepheus`` device
directory (Device/xiaomi-cepheus with its own DeviceTreeBlob), so unlike
guacamole we do NOT have to clone another vendor's device dir. The DSC selects
it purely via ``TARGET_DEVICE=xiaomi-cepheus`` and PlatformBuildNoSb derives
``FDT=cepheus.dtb`` from it, both of which board.toml already sets.

The aloha-cepheus.patch creates ``Resources/Configs/guacamole.toml``; we
mirror it to ``cepheus.toml`` because board.toml sets ``[firmware] config``
to "cepheus".
"""
import os
import shutil
import sys
import tomllib
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "unixnu" / "scripts"))
sys.path.insert(0, str(ROOT / "scripts"))

from unixnu import firmware  # noqa: E402
from tree import assemble  # noqa: E402

UEFI = ROOT / "uefi"
EDK2 = UEFI / "MU_BASECORE"
WORK = ROOT / "out" / "cepheus"
TREE = assemble(WORK, ROOT / "unixnu", ROOT)
LOGS = WORK / "logs"
LOGS.mkdir(parents=True, exist_ok=True)

DEVICE_ROOT = UEFI / "Platforms" / "SurfaceDuo1Pkg" / "Device"
DST = DEVICE_ROOT / "xiaomi-cepheus"
SRC = DEVICE_ROOT / "oneplus-guacamole"

# xiaomi-cepheus is shipped by the Aloha tree; only synthesise it as a fallback
# (same SM8150 silicon) if a future revision drops it.
if not DST.is_dir():
    if not SRC.is_dir():
        raise SystemExit(
            f"firmware: neither {DST} nor {SRC} exists in the Aloha tree; "
            "cannot build the cepheus device."
        )
    print(f"firmware: {DST.name} missing; cloning {SRC.name} (same SM8150 silicon)")
    shutil.copytree(SRC, DST)
    for old, new in (("guacamole.dtb", "cepheus.dtb"), ("guacamole.toml", "cepheus.toml")):
        p = DST / old
        if p.is_file():
            p.rename(DST / new)
else:
    print(f"firmware: using native Aloha device dir {DST.name}")

# The UniXNU firmware driver reads Resources/Configs/<config>.toml; the SM8150
# patch only provides the guacamole one.
cfg_src = UEFI / "Resources" / "Configs" / "guacamole.toml"
cfg_dst = UEFI / "Resources" / "Configs" / "cepheus.toml"
if cfg_src.is_file() and not cfg_dst.is_file():
    shutil.copy(cfg_src, cfg_dst)
    print(f"firmware: created {cfg_dst.name}")

# The FDF's flash device must equal the "UEFI FD" entry of the cepheus memory
# map, and the FV only fits in 3 MiB when built RELEASE. See
# scripts/prepare-cepheus-fv.py.
import subprocess  # noqa: E402
subprocess.run([sys.executable, str(ROOT / "scripts" / "prepare-cepheus-fv.py"),
                str(UEFI)], check=True)

board = tomllib.loads((ROOT / "board.toml").read_text())
ctx = types.SimpleNamespace(
    board=board,
    tree=TREE,
    work=WORK,
    logs=LOGS,
    jobs=8,
    args=types.SimpleNamespace(skip_preflight=False),
    uefi=UEFI,
    device=ROOT,  # newer UniXNU preflight passes UNIXNU_DEVICE to its scripts
)
# RELEASE is required, not just preferred: the cepheus FV only fits inside the
# 3 MiB the memory map reserves when built RELEASE (DEBUG comes out at
# 0x308610+ and GenFv refuses it). RELEASE is what the raphael reference port
# ships too.
TARGET = os.environ.get("FW_TARGET", "RELEASE")
firmware.build(ctx, None, UEFI, EDK2, TARGET)
