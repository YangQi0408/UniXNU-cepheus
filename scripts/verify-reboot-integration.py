#!/usr/bin/env python3
"""Check inventory, a private pre-feature XNU copy, or the applied build tree.

Inventory alone does NOT check patch application. CI runs the applied-tree mode
after the full XNU build; Linux can apply-check with --xnu-tree PATH. No mode
modifies the supplied source tree. Missing explicit source input is an error.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
XNU = ROOT / "xnu"
PATCHES = (
    "0086-cepheus-reboot.patch", "0087-cepheus-fastboot.patch",
    "0088-entropy-health-ready.patch", "0089-sweet-prng-readiness.patch",
)
SOURCES = ("cepheus_reboot.c", "cepheus_reboot.h", "cepheus_fastboot.c", "cepheus_fastboot.h")
REQUIRED = PATCHES + SOURCES + ("0086-stage-cepheus-reboot.sh", "0087-stage-cepheus-fastboot.sh")


def inventory():
    for name in REQUIRED:
        if not (XNU / name).is_file() or not (XNU / name).stat().st_size:
            raise ValueError(f"missing integration file: {name}")
    endpoint = (XNU / "cepheus_fastboot.c").read_text()
    for marker in ('#include "cepheus_fastboot_private.h"',
                   "CEPHEUS_FASTBOOT_PROVISIONED 0", "IP_BOUND_IF", "IP_PKTINFO",
                   "MSG_TRUNC", "MSG_CTRUNC", "sweet_prng_entropy_ready",
                   "cepheus_fastboot_budget_take", "ifnet_release",
                   "cepheus_fastboot_request_stop", "thread_terminate(current_thread())"):
        if marker not in endpoint:
            raise ValueError(f"missing source marker: {marker}")
    if (XNU / "cepheus_fastboot_private.h").exists():
        raise ValueError("private key header must not be in source repository")


def applied(tree):
    for name in SOURCES:
        if (tree / "bsd/dev" / name).read_bytes() != (XNU / name).read_bytes():
            raise ValueError(f"staged source mismatch: {name}")
    conf = (tree / "bsd/conf/files").read_text()
    for name in ("cepheus_reboot.c", "cepheus_fastboot.c"):
        if sum(line.split() == [f"bsd/dev/{name}", "standard"] for line in conf.splitlines()) != 1:
            raise ValueError(f"missing/duplicate conf entry: {name}")
    usb = (tree / "bsd/dev/sweet_usb.c").read_text()
    if 'if (fastboot == 1 && ud.ifp != NULL) cepheus_fastboot_start(ud.ifp);' not in usb:
        raise ValueError("USB startup hook missing")
    entropy = (tree / "osfmk/prng/entropy.c").read_text()
    header = (tree / "osfmk/prng/entropy.h").read_text()
    prng = (tree / "osfmk/prng/prng_random.c").read_text()
    if (entropy.count("\nint\nentropy_health_is_ready(void)\n{") != 1 or
            "\nvoid\nentropy_collect(void)\n{" not in entropy or
            "\nvoid\nint\n" in entropy or
            header.count("int entropy_health_is_ready(void);") != 1):
        raise ValueError("entropy readiness patch misplaced or incomplete")
    if (prng.count("\nint\nsweet_prng_entropy_ready(void)\n{") != 1 or
            prng.count("sweet_prng_credited_reseed = true;") != 1):
        raise ValueError("credited PRNG reseed hook missing")


def apply_check(reference):
    # Only a few real source files are needed. Caller supplies a tree with SWEET
    # patches (through0085), NOT a tree with the feature already applied.
    with tempfile.TemporaryDirectory(prefix="cepheus-patch-check-") as directory:
        tree = Path(directory)
        for name in ("bsd/conf/files", "bsd/dev/sweet_usb.c", "osfmk/prng/entropy.c",
                     "osfmk/prng/entropy.h", "osfmk/prng/prng_random.c"):
            target = tree / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(reference / name, target)
        # Existing local reference uses original touch names; stage predecessor
        # conf-only patches on the PRIVATE copy if needed, never on reference.
        conf = (tree / "bsd/conf/files").read_text()
        subprocess.run(["git", "init", "-q", str(tree)], check=True)
        for patch in ("0082-cepheus-touch.patch", "0085-cepheus-soc.patch"):
            if ((patch.startswith("0082") and "bsd/dev/cepheus_touch.c" not in conf) or
                    (patch.startswith("0085") and "bsd/dev/cepheus_soc.c" not in conf)):
                subprocess.run(["git", "-C", str(tree), "apply", str(XNU / patch)], check=True)
        for patch in PATCHES:
            subprocess.run(["git", "-C", str(tree), "apply", "--check", str(XNU / patch)], check=True)
            subprocess.run(["git", "-C", str(tree), "apply", str(XNU / patch)], check=True)
        for name in SOURCES:
            shutil.copyfile(XNU / name, tree / "bsd/dev" / name)
        applied(tree)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--inventory-only", action="store_true")
    mode.add_argument("--xnu-tree", type=Path, help="pre-feature SWEET source; never modified")
    mode.add_argument("--applied-tree", type=Path, help="post-build source; never modified")
    args = parser.parse_args()
    inventory()
    if args.xnu_tree:
        apply_check(args.xnu_tree)
        print("real-source patch application and applied placement: PASS (not a full build)")
    elif args.applied_tree:
        applied(args.applied_tree)
        print("applied build-source validation: PASS")
    else:
        print("integration inventory: PASS (patch application NOT checked)")


if __name__ == "__main__":
    main()
