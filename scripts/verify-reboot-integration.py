#!/usr/bin/env python3
"""Fail-fast offline checks for the refusal-gated reboot patch inventory.

This validates repository-owned staging artifacts and patch structure only. It
cannot establish a macOS XNU build, platform reset ABI, or hardware behavior.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
XNU = ROOT / "xnu"
REQUIRED = (
    "0086-stage-cepheus-reboot.sh", "0086-cepheus-reboot.patch",
    "cepheus_reboot.c", "cepheus_reboot.h", "0087-stage-cepheus-fastboot.sh",
    "0087-cepheus-fastboot.patch", "cepheus_fastboot.c", "cepheus_fastboot.h",
    "0088-entropy-health-ready.patch",
)


def main():
    for name in REQUIRED:
        path = XNU / name
        if not path.is_file() or not path.stat().st_size:
            raise SystemExit(f"missing reboot integration file: {path}")
    patch = (XNU / "0086-cepheus-reboot.patch").read_text()
    for marker in ("bsd/conf/files", "bsd/dev/cepheus_reboot.c"):
        if marker not in patch:
            raise SystemExit(f"reboot patch missing marker: {marker}")
    with tempfile.TemporaryDirectory() as directory:
        tree = Path(directory)
        (tree / "bsd/conf").mkdir(parents=True)
        (tree / "bsd/conf/files").write_text(
            "bsd/dev/sweet_keys.c\t\t\tstandard\n"
            "bsd/dev/cepheus_touch.c\t\t\tstandard\n"
            "bsd/dev/cepheus_soc.c\t\t\tstandard\n"
            "bsd/dev/sweet_rpmh.c\t\t\tstandard\n"
            "bsd/dev/sweet_spmi.c\t\t\tstandard\n")
        subprocess.run(["git", "init", "-q", str(tree)], check=True)
        subprocess.run(["git", "-C", str(tree), "add", "."], check=True)
        subprocess.run(["git", "-C", str(tree), "apply", "--check", str(XNU / "0086-cepheus-reboot.patch")], check=True)
        subprocess.run(["git", "-C", str(tree), "apply", str(XNU / "0086-cepheus-reboot.patch")], check=True)
        reference = Path("/home/yangqi/cepheus-port/kernel-test/xnu")
        (tree / "bsd/dev").mkdir(parents=True)
        (tree / "bsd/dev/sweet_usb.c").write_bytes((reference / "bsd/dev/sweet_usb.c").read_bytes())
        (tree / "osfmk/prng").mkdir(parents=True)
        for name in ("entropy.c", "entropy.h"):
            (tree / "osfmk/prng" / name).write_bytes((reference / "osfmk/prng" / name).read_bytes())
        subprocess.run(["git", "-C", str(tree), "add", "."], check=True)
        subprocess.run(["git", "-C", str(tree), "apply", "--check", str(XNU / "0087-cepheus-fastboot.patch")], check=True)
        subprocess.run(["git", "-C", str(tree), "apply", "--check", str(XNU / "0088-entropy-health-ready.patch")], check=True)
    fastboot_patch = (XNU / "0087-cepheus-fastboot.patch").read_text()
    for marker in ("bsd/conf/files", "bsd/dev/cepheus_fastboot.c", "sweet_usb.c", "sweetfastboot"):
        if marker not in fastboot_patch:
            raise SystemExit(f"fastboot patch missing marker: {marker}")
    endpoint = (XNU / "cepheus_fastboot.c").read_text()
    if '#include "cepheus_fastboot_private.h"' not in endpoint or "CEPHEUS_FASTBOOT_PROVISIONED 0" not in endpoint:
        raise SystemExit("private key include or public unprovisioned fallback is missing")
    if "entropy_health_is_ready" not in (XNU / "0088-entropy-health-ready.patch").read_text() or "entropy_health_is_ready" not in endpoint:
        raise SystemExit("synchronized entropy readiness hook is missing")
    for marker in ("CEPHEUS_FASTBOOT_PROVISIONED 0", "IP_BOUND_IF", "IP_PKTINFO", "MSG_TRUNC", "MSG_CTRUNC", "cepheus_fastboot_make_response"):
        if marker not in endpoint:
            raise SystemExit(f"fastboot runtime safety marker missing: {marker}")
    if "CEPHEUS_FASTBOOT_PROVISIONED 0" in (ROOT / "scripts/stage-private-fastboot-key.py").read_text():
        raise SystemExit("private staging script contains a default key")
    print("reboot and optional fastboot integration inventory: PASS")


if __name__ == "__main__":
    main()
