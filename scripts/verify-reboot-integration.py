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
    "cepheus_reboot.c", "cepheus_reboot.h",
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
    print("reboot integration inventory and patch apply-check: PASS")


if __name__ == "__main__":
    main()
