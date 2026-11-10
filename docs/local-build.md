# Local build workflow

The current SWEET XNU 8796 build uses the pinned macOS builder. Firmware
compilation, patch preparation, host regressions, FV reconstruction, FAT/boot
packaging and independent artifact verification run locally.

## Kernel preflight

Run from `gpu-work` with the pinned source inputs already present:

```sh
python3 scripts/local-kernel-preflight.py --output out/kernel-preflight-NEW
```

This archives ios-sweet commit `45263bee51df4a0f55cdf9c649af886399488ddb`
into a fresh output directory, stages Cepheus sources, generates patches and
runs the host checks. It records source hashes, commands, logs and results in
`receipt.json`. It does not modify the reference checkout. The initial passing
run is `out/local-kernel-preflight-v2/receipt.json`; the final v4 run also
includes the missing power-MMIO staging input and passes 65 host programs
and seven production-unit static analyses. Generated patches stay
separate because regression tests reconstruct the untransformed reference.

## Pair candidate

After obtaining and verifying a compiled pair kernel:

```sh
python3 scripts/build-scm-pair-local.py \
  --kernel ../inputs/ci-RUN/kernel.macho \
  --kernel-receipt out/kernel-verification-NEW.json \
  --output out/scm-pair-NEW \
  --container cepheus-build:latest
```

The command builds DarwinBoot with local EDK2/Clang, reconstructs the accepted
firmware volume, replaces only DarwinBoot, packages the boot and independently
verifies its contents. Container packaging uses no network and the current UID.
It preserves the accepted boot's shim, DTB and unrelated FAT files. It adds one
zero-filled 2656-byte `/chosen/cepheus-scm-handoff` slot and replaces only
`cephzapmem=1` with `cephscmpair=1` in the existing boot profile.

Each output directory must be new. A failed directory is retained as evidence.
The kernel input still requires its own compiled-artifact verification; markers
alone cannot establish that an input is a working XNU build.

The first real end-to-end run completed successfully on 2026-11-09:

```sh
python3 scripts/verify-ci-scm-pair.py \
  --run 42632917379 --commit 6ce4c7c \
  --input ../inputs/ci-42632917379 \
  --log ../logs/scm-pair-ci-42632917379/build.log \
  --output out/kernel-verification-ci-42632917379.json
python3 scripts/build-scm-pair-local.py \
  --kernel ../inputs/ci-42632917379/kernel.macho \
  --kernel-receipt out/kernel-verification-ci-42632917379.json \
  --output out/scm-pair-final-6ce4c7c \
  --container cepheus-build:latest
```

These output paths already exist and are evidence; choose new paths to rerun.
Final boot: `out/scm-pair-final-6ce4c7c/boot-scm-pair.img`, 70688768 bytes,
SHA256 `2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2`.
Its `receipt.json` and independent `boot-verification.json` both pass. No phone
operations occurred. See [scm-pair-preflight.md](scm-pair-preflight.md) for scope
and the next hardware point; this is not a GPU execution image.

## Exit-Tail Candidate

The isolated exit-tail profile reuses two complete local firmware builds,
revalidates their exact staged sources and artifacts, then packages both
DarwinBoot and DXE Core. It requires a newly compiled tail-aware XNU kernel,
not the previously accepted BCM kernel. Verify that new CI kernel with
`verify-ci-scm-pair.py --rpmh-tail`, then run:

```sh
python3 scripts/build-scm-pair-local.py \
  --kernel ../inputs/ci-RUN/kernel.macho \
  --kernel-receipt out/kernel-verification-NEW.json \
  --output out/rpmh-tail-NEW --rpmh-tail \
  --firmware-build out/rpmh-tail-loader-20261110-v3 \
  --core-build out/rpmh-tail-core-platform-20261110-v3 \
  --container cepheus-build:latest
```

The 1128-byte zero slot and `cephrpmhtail=1` are isolated from SCM/snapshot
profiles. Independent packaging verification checks both embedded EFI binaries
and unchanged shim/DTB/FAT/touch settings. This remains diagnostic only;
neither packaging nor offline fixtures prove hardware GPU execution.
The earlier isolated core build has no boot LZMA handler and is rejected by
the updated core validator. Use the platform-compatible build above; its
generated PCD/constructor contract, library set and exact compile/link options
for the core and all 40 libraries are checked against the accepted RELEASE
core as part of packaging acceptance. Platform v2 is also superseded: it lacks
the accepted 4 KiB runtime granularity definition and is now rejected.

Full tail-aware XNU CI `42835691416 @2bf48ab` and independent local receipt
`out/kernel-verification-ci-42835691416.json` passed. The genuine kernel is in
`../inputs/ci-42835691416/kernel.macho`; firmware is built locally. This does
not establish a successful diagnostic phone boot or GPU execution.

## Full Linux XNU

On 2026-11-09 the host dependency check reported missing `tcsh`, `unifdef`,
`mformat` and `mcopy`. No configured `mig`, `iig`, Mach-O ld64 or XNU-specific
Clang toolchain was found. The inspected UniXNU Linux tooling at commit
`350ddc2a23dcaea7d74d8a469259c2efcec3e4a9` targets XNU 10063/iOS 17; the
accepted Cepheus kernel uses XNU 8796/SWEET. Its toolchain and SDK preparation
need adaptation and an actual compile/link check before a local kernel can
replace the current macOS artifact. The upstream installer also writes shims
to `/usr/bin`; it has not been executed or installed on this host.

Full XNU remains in CI for this candidate. A future local migration should use
an isolated prefix/container and retain the current XNU version, board config,
patch order, SDK ABI and compiled-artifact checks.
