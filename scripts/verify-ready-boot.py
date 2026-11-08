#!/usr/bin/env python3
"""Read-only boot/payload validation; writes a new receipt, never uses a phone."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import tomllib


def digest(data):
    return hashlib.sha256(data).hexdigest()


def validate_args(line, memory_test=None, touch_only=False, soc_only=False, soc_fuse=False, soc_fuse_arc=False, all_probes=False, scm_info=False, soc_delay=False):
    assert sum((touch_only, soc_only, soc_fuse, soc_fuse_arc, all_probes)) <= 1, "choose one isolation profile"
    tokens = line.split()
    names = [t.split("=", 1)[0] for t in tokens]
    assert len(names) == len(set(names)), "duplicate boot arguments"
    assert len(line) <= 1000
    forbidden = {"cephmem", "cephboost", "cephgpu", "cephboot", "cephpower", "sweetfastboot", "sweetgpu", "sweettouchreset"}
    if soc_delay:
        assert soc_only and memory_test is None and not scm_info, "SoC delay requires basic-only profile"
    else:
        forbidden.add("cephsocdelay")
    if scm_info:
        assert (soc_only or all_probes) and memory_test is None, "SCM query requires explicit basic or full read-only baseline profile"
    else:
        forbidden.add("cephscm")
    if memory_test is not None:
        assert memory_test in (1, 2) and not (touch_only or soc_only or soc_fuse or soc_fuse_arc or all_probes)
        forbidden.remove("cephmem")
        assert f"cephmem={memory_test}" in tokens
    required = {"sweetusb=2", "sweettouch=1", "sweetkeys=1", "sweetkeyspononly=1",
                "sweettouchgpio=1", "sweettouchpower=1", "sweettouchdelay=60",
                "sweethidonce=1", "sweetfb_noconsole=1"}
    if scm_info:
        required.add("cephscm=1")
    if soc_delay:
        required.add("cephsocdelay=240")
    if touch_only or soc_only or soc_fuse or soc_fuse_arc or all_probes:
        if all_probes:
            required.add("cephgx=1")
        else:
            forbidden.add("cephgx")
        if soc_fuse_arc or all_probes:
            required.add("cepharc=1")
        else:
            forbidden.add("cepharc")
        if soc_fuse or soc_fuse_arc or all_probes:
            required.add("cephfuse=1")
        else:
            forbidden.add("cephfuse")
        if touch_only:
            forbidden.add("cephsoc")
        else:
            required.add("cephsoc=1")
        required.add("sweettouchdiag=1")
    else:
        required.update({"cephgx=1", "cephsoc=1"})
    assert not forbidden.intersection(names)
    assert required.issubset(tokens)


def unpack(path, directory):
    data = path.read_bytes()
    assert data[:8] == b"ANDROID!"
    kernel_size, _, ramdisk_size, _, second, _, _, page = struct.unpack_from("<8I", data, 8)
    assert page == 4096 and not second and struct.unpack_from("<I", data, 40)[0] == 0
    offset = page + ((kernel_size + page - 1) // page) * page
    assert offset + ramdisk_size <= len(data)
    sizes = struct.pack("<III", kernel_size, ramdisk_size, second)
    expected = hashlib.sha1(data[page:page + kernel_size] + data[offset:offset + ramdisk_size] + sizes).digest()[:8]
    assert data[576:584] == expected
    directory.mkdir()
    fat = directory / "fat.img"
    fat.write_bytes(data[offset:offset + ramdisk_size])
    subprocess.run(["mcopy", "-s", "-i", str(fat), "::/EFI", str(directory)], check=True)
    files = {str(p.relative_to(directory / "EFI")): p.read_bytes()
             for p in (directory / "EFI").rglob("*") if p.is_file()}
    return data, offset, ramdisk_size, files


def validate_added_args(config, reference, added):
    assert added and len(added) == len(set(added)), "unique added arguments required"
    old = reference["entry"][0]["args"].split()
    new = config["entry"][0]["args"].split()
    old_names = {token.split("=", 1)[0] for token in old}
    added_names = [token.split("=", 1)[0] for token in added]
    assert len(added_names) == len(set(added_names)), "duplicate added argument names"
    assert not old_names.intersection(added_names), "argument already present in reference"
    assert all(new.count(token) == 1 for token in added), "added argument missing or duplicated"
    assert [token for token in new if token not in added] == old, "other arguments changed"
    # Compare every TOML field, allowing only the verified entry's args string.
    normalized = {**config, "entry": [dict(entry) for entry in config["entry"]]}
    normalized["entry"][0]["args"] = reference["entry"][0]["args"]
    assert normalized == reference, "other config fields changed"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("kernel", type=Path)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--memory-test", type=int, choices=(1, 2),
                        help="explicit CPU-only memory diagnostic profile; default remains disabled")
    isolation = parser.add_mutually_exclusive_group()
    isolation.add_argument("--touch-only", action="store_true",
                        help="require bounded touch diagnostics with all cephsoc/GPU probes and memory tests absent")
    isolation.add_argument("--soc-only", action="store_true",
                        help="require only cephsoc=1 plus touch diagnostics, without ARC/fuse/GX/boost/memory subprofiles")
    isolation.add_argument("--soc-fuse", action="store_true",
                        help="require cephsoc=1 and cephfuse=1 with touch diagnostics; no ARC/GX/boost/memory")
    isolation.add_argument("--soc-fuse-arc", action="store_true",
                        help="require cephsoc/fuse/arc=1 with touch diagnostics; no GX/boost/memory")
    isolation.add_argument("--all-probes", action="store_true",
                        help="require cephsoc/fuse/arc/gx=1 with touch diagnostics; no boost/memory")
    parser.add_argument("--scm-info", action="store_true",
                        help="with --soc-only or --all-probes, require explicit cephscm=1 fixed INFO/PAS-supported queries")
    parser.add_argument("--soc-delay", action="store_true",
                        help="with --soc-only, require fixed cephsocdelay=240 and compiled phase markers")
    parser.add_argument("--only-added-arg", action="append", default=[], metavar="TOKEN",
                        help="also require unchanged reference kernel/config except these added args")
    args = parser.parse_args()
    assert not args.receipt.exists(), "keep existing evidence"
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        image, offset, size, files = unpack(args.image, root / "new")
        reference, old_offset, old_size, originals = unpack(args.reference, root / "reference")
        assert (offset, size, len(image)) == (old_offset, old_size, len(reference))
        assert image[:576] == reference[:576]
        assert image[584:offset] == reference[584:offset]
        assert image[offset + size:] == reference[offset + size:]
        assert files.keys() == originals.keys()
        mutable = {"UniXNU/unixnu.toml", "UniXNU/Payloads/kernel.macho"}
        for name in files.keys() - mutable:
            assert files[name] == originals[name], name
        assert files["UniXNU/Payloads/kernel.macho"] == args.kernel.read_bytes()
        config = tomllib.loads(files["UniXNU/unixnu.toml"].decode())
        line = config["entry"][0]["args"]
        validate_args(line, args.memory_test, args.touch_only, args.soc_only, args.soc_fuse, args.soc_fuse_arc, args.all_probes, args.scm_info, args.soc_delay)
        if args.scm_info:
            assert all(marker in files["UniXNU/Payloads/kernel.macho"] for marker in
                       (b"cephscm", b"CEPHEUS SCM INFO: begin", b"CEPHEUS SCM INFO REGS:",
                        b"CEPHEUS SCM QUERY7: confirmed=", b"normal64 calls=7")), "missing compiled QUERY7 evidence markers"
        if args.soc_delay:
            kernel = files["UniXNU/Payloads/kernel.macho"]
            assert all(marker in kernel for marker in
                       (b"cephsocdelay", b"CEPHEUS SOC: %s delay_s=%u uptime_ms=%llu",
                        b"baseline-end", b"CEPHEUS SOC: refused delay profile")), "missing compiled delay guard/phase markers"
        if args.only_added_arg:
            assert files["UniXNU/Payloads/kernel.macho"] == originals["UniXNU/Payloads/kernel.macho"], "reference kernel changed"
            validate_added_args(config, tomllib.loads(originals["UniXNU/unixnu.toml"].decode()), args.only_added_arg)
        receipt = {"image": str(args.image), "bytes": len(image), "sha256": digest(image),
                   "reference": str(args.reference), "reference_sha256": digest(reference),
                   "kernel_sha256": digest(args.kernel.read_bytes()),
                   "adt_sha256": digest(files["UniXNU/Payloads/kernel.macho.adt"]),
                   "args": line, "all_other_payload_files_unchanged": True,
                   "firmware_dtb_boot_header_unchanged": True, "boot_checksum_valid": True,
                   "hardware_tested": False,
                   "memory_test": args.memory_test,
                   "touch_only": args.touch_only,
                   "soc_only": args.soc_only,
                   "soc_probe_delay_s": 240 if args.soc_delay else None,
                   "soc_fuse": args.soc_fuse,
                   "soc_fuse_arc": args.soc_fuse_arc,
                   "all_probes": args.all_probes,
                   "scm_info": args.scm_info,
                   "scope": "Packaging only. Explicit CPU-only memory selftest enabled." if args.memory_test else
                       "Packaging only. Memory selftest disabled.",
                   "cpu_boost_gpu_startup_disabled": True, "phone_operations": False}
        if args.only_added_arg:
            receipt.update({"kernel_unchanged_from_reference": True,
                            "only_config_change_added_args": args.only_added_arg})
    with args.receipt.open("x") as output:
        json.dump(receipt, output, indent=2)
        output.write("\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
