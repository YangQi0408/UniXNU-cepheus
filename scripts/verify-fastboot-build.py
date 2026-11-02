#!/usr/bin/env python3
"""Recompile both key branches with the actual XNU compiler invocation.

macOS post-build check, not a second kernel link or a hardware test. All fixture
files/objects live in an automatically deleted temporary directory; the public
source, object and kernel are never overwritten. No caller-supplied key is read.
Undefined symbols from both objects must resolve in the fully linked public
kernel. The synthetic object is NEVER a published/deployable artifact.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SYNTHETIC_KEY = bytes(range(32, 64))  # Public test vector, not a credential.
ANSI = re.compile(r"\x1b\[[0-9;]*m")
RUNTIME_SYMBOLS = {
    "_cepheus_fastboot_bootstrap", "_cepheus_fastboot_start",
    "_cepheus_fastboot_hmac_sha256", "_cepheus_fastboot_pack",
    "_cepheus_fastboot_unpack", "_entropy_health_is_ready",
    "_sweet_prng_entropy_ready",
}
REQUIRED_IMPORTS = {
    "_sock_receive", "_sock_send", "_sock_bind", "_sock_close",
    "_sock_setsockopt", "_ifnet_reference", "_ifnet_release",
    "_cchmac", "_ccsha256_ltc_di", "_entropy_health_is_ready",
    "_sweet_prng_entropy_ready", "_read_random", "_thread_terminate",
}


def compile_command(log: str, source: Path) -> list[str]:
    """Accept exactly one direct compile of the intended absolute source."""
    matches = []
    for line in log.splitlines():
        if str(source) not in line or " -c " not in line:
            continue
        try:
            args = shlex.split(ANSI.sub("", line))
        except ValueError:
            continue
        if (args and Path(args[0]).name == "clang" and "-c" in args and
                args.count(str(source)) == 1):
            matches.append(args)
    if len(matches) != 1:
        raise ValueError(f"expected one endpoint compile command, found {len(matches)}")
    args = matches[0]
    for required in ("-DKERNEL", "-DBSD_KERNEL_PRIVATE", "-fno-builtin", "-O2"):
        if required not in args:
            raise ValueError(f"actual kernel compile lacks {required}")
    return args


def fixture_command(args: list[str], source: Path, fixture: Path, out: Path) -> list[str]:
    """Remove dependency/output paths, retaining all real kernel flags/includes."""
    result = []
    index = 0
    paired = ("-o", "-MF", "-MT", "-MQ", "-MJ")
    while index < len(args):
        item = args[index]
        if item in paired:
            if index + 1 == len(args):
                raise ValueError(f"missing value for {item}")
            index += 2
            continue
        if item in ("-MD", "-MMD", "-MP", "-MG"):
            index += 1
            continue
        # Avoid accidentally writing dependencies into the original object dir.
        if any(item.startswith(flag) and item != flag for flag in paired[1:]):
            index += 1
            continue
        if item.startswith(("-Wp,", "@")):
            raise ValueError("unhandled dependency/response-file option")
        result.append(str(fixture) if item == str(source) else item)
        index += 1
    return result + ["-Werror=missing-prototypes", "-o", str(out)]


def defined_symbols(text: str) -> set[str]:
    symbols = set()
    for line in text.splitlines():
        parts = line.split()
        if (len(parts) == 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]) and
                parts[1].upper() != "U"):
            symbols.add(parts[2])
    return symbols


def undefined_symbols(text: str) -> set[str]:
    symbols = set()
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 1 and parts[0].startswith("_"):
            symbols.add(parts[0])  # Darwin nm -u
        elif len(parts) == 2 and parts[0] == "U":
            symbols.add(parts[1])  # LLVM nm -u
        elif parts:
            raise ValueError(f"unrecognized undefined-symbol output: {line}")
    return symbols


def check_imports(imports: set[str], kernel_symbols: set[str]) -> None:
    missing_runtime = REQUIRED_IMPORTS - imports
    unresolved = imports - kernel_symbols
    if missing_runtime:
        raise ValueError(f"runtime compiled out: {sorted(missing_runtime)}")
    if unresolved:
        raise ValueError(f"imports absent from linked kernel: {sorted(unresolved)}")


def run(dxb: Path, kernel: Path) -> None:
    dxb, kernel = dxb.resolve(strict=True), kernel.resolve(strict=True)
    source = dxb / "xnu/bsd/dev/cepheus_fastboot.c"
    if (source.parent / "cepheus_fastboot_private.h").exists():
        raise ValueError("check requires an unprovisioned public source tree")
    for name in ("cepheus_fastboot.c", "cepheus_fastboot.h"):
        if (source.parent / name).read_bytes() != (ROOT / "xnu" / name).read_bytes():
            raise ValueError(f"public source mismatch: {name}")
    objects = list((dxb / "build/xnu.obj").rglob("cepheus_fastboot.o"))
    if len(objects) != 1:
        raise ValueError("expected exactly one compiled public endpoint object")
    args = compile_command((dxb / "build.log").read_text(), source)
    nm = subprocess.check_output(["xcrun", "--find", "nm"], text=True).strip()
    kernel_symbols = defined_symbols(subprocess.check_output([nm, str(kernel)], text=True))
    if RUNTIME_SYMBOLS - kernel_symbols:
        raise ValueError(f"linked runtime symbols missing: {sorted(RUNTIME_SYMBOLS - kernel_symbols)}")
    if undefined_symbols(subprocess.check_output([nm, "-u", str(kernel)], text=True)):
        raise ValueError("public kernel contains undefined symbols")
    protected = [kernel, objects[0], source, source.with_suffix(".h")]
    before = {p: hashlib.sha256(p.read_bytes()).digest() for p in protected}
    spec = importlib.util.spec_from_file_location("key_stage", ROOT / "scripts/stage-private-fastboot-key.py")
    stage = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(stage)
    # macOS /var is a symlink: the key stager deliberately requires resolved paths.
    with tempfile.TemporaryDirectory(prefix="cepheus-build-fixture-") as directory:
        base = Path(directory).resolve()
        for provisioned in (0, 1):
            private = base / f"branch-{provisioned}"
            dev = private / "bsd/dev"
            dev.mkdir(parents=True)
            (private / "bsd/conf").mkdir()
            shutil.copyfile(dxb / "xnu/bsd/conf/files", private / "bsd/conf/files")
            for name in ("sweet_usb.c", "cepheus_fastboot.c", "cepheus_fastboot.h"):
                shutil.copyfile(source.parent / name, dev / name)
            if provisioned:
                key = base / "synthetic.input"
                with key.open("xb") as handle:
                    key.chmod(0o600)
                    handle.write(SYNTHETIC_KEY)
                stage.stage(key, private)
            fixture = dev / source.name
            with fixture.open("a") as handle:
                handle.write(f'\n_Static_assert(CEPHEUS_FASTBOOT_PROVISIONED == {provisioned}, "wrong key branch");\n')
            out = private / "endpoint.o"
            subprocess.run(fixture_command(args, source, fixture, out),
                           cwd=objects[0].parent, check=True)
            imports = undefined_symbols(subprocess.check_output([nm, "-u", str(out)], text=True))
            check_imports(imports, kernel_symbols)
            if provisioned and SYNTHETIC_KEY not in out.read_bytes():
                raise ValueError("synthetic key missing from compiled fixture")
            print(f"branch={provisioned}: actual XNU compile and linked-symbol resolution PASS")
    if any(hashlib.sha256(p.read_bytes()).digest() != digest for p, digest in before.items()):
        raise ValueError("public build files changed during fixture validation")
    print("fixtures deleted; public kernel/object/source unchanged; not a second full link or hardware test")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dxb", type=Path)
    parser.add_argument("kernel", type=Path)
    args = parser.parse_args()
    run(args.dxb, args.kernel)


if __name__ == "__main__":
    main()
