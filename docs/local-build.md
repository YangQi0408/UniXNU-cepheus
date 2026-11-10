# Local build workflow

The current SWEET XNU 8796 build uses the pinned macOS builder. Firmware
compilation, patch preparation, host regressions, FV reconstruction, FAT/boot
packaging and independent artifact verification run locally.

## Kernel preflight

2026-11-11: complete RPMh-scope run `42874717231 @091c7ff` succeeded at
02:55:48 and passes independent archived-source/kernel verification. Kernel
17473184 bytes, SHA256
`677de398a71e9b06b8eab312293b3bd2d977b8870dce5fd0c20a3cbc1d257299`.
97 core/30 memory entries, no unresolved, touch/PA64/native SCM/tail PASS.
Actual linked RPMh scope and idle checks also pass; ordinary and optimized
RPMh receipts match. See `rpmh-writer-coordination.md` for exact receipts.

GPLL0 token/lifecycle integration was separately committed/pushed as `790e0e4`.
Focused final-v2 PASS (SHA256 `88f257228b5db0e6e83035ec6ec08d71fb9b9de163efbb3a6098598615054d90`)
and full preflight final-v2 PASS (SHA256 `5af17ec7ea7d7e3bc0b6daf632b7764eab79da0eb506c77adba0ecaeec423de2`),
four prerequisites/77 host/seven analyses. Prior include failure remains
archived. This later batch still needs its own full XNU link acceptance and
is NOT in CI42874717231. No phone operation or GPU execution.

2026-11-11 02:15 update: complete idle CI `42867280759 @91a5ec1` succeeded
and independently passes archived-source/touch/PA64/SCM/tail plus actual linked
idle control-flow checks. Kernel 17472904 bytes, SHA256
`3a77999f46e307ed4b2353989dd2c1ae339cc9c643a2178c42a7c9bf2e8e59f1`.
Receipts `out/kernel-verification-ci-42867280759.json` and
`out/idle-linked-ci-42867280759-v1.json`. No phone boot or new image.
The subsequent `rpmh-writer-coordination.md` batch is separate and not accepted
by this kernel. Local focused scope/adapters and final-v2 full preflight pass
(four prerequisites/77 host/seven analyses, SHA256 `f31e8502e85a410a1c3d2c55d404df8d77fe43b33c61dcb8d21781a8f373ed02`).

2026-11-11 01:33 combined native kernel acceptance supersedes the pending
link notes below: CI `42860421403 @d22a054` succeeded. Downloaded kernel is
17472504 bytes, SHA256
`801079f0e46560de5ee4c2cd0ff5d36bb5b56402012cf42f078fec610c457656`.
Local `out/kernel-verification-ci-42860421403.json` independently passes
94 core/30 memory entries, no unresolved symbols, exact archived source,
touch/PA64/native SCM and tail checks. New scoped idle code is not in that
build. Firmware and all host/preflight/packaging work still run locally.

Scoped idle work: `out/gpu-idle-local-20261111-accepted/receipt.json` accepts
10000 eight-reader pthread races, native lifecycle and actual applied CPU/
machine-idle bodies with host/ASan/UBSan, native AArch64 and static analysis.
See `gpu-idle-coordination.md`; full XNU link/runtime of this later batch is
pending under CI `42867280759 @91a5ec1` (dispatched once, 01:40:04), no startup
caller or phone operation. At 01:48 its regression/static analysis passed;
codegen was in progress, not complete kernel acceptance. The existing diagnostic boot
image remains bound to CI42835691416, not this newer kernel.

2026-11-11 native upload continuation:
`out/upload-native-local-20261111-final-v1/receipt.json` accepts fixed native
maps, 8433 PROT_NONE boundaries, real-pool/snapshot bindings and staged AArch64.
Explicit GMU/SQE ASan/UBSan native+actual-pool and whole-chain runs pass with
emulated hardware. See `upload-native-backend.md` for exact hashes and limits.
Full combined XNU compile/link is the next artifact gate, not GPU execution.
`out/upload-native-preflight-20261111-final-v1/receipt.json` passes all four
local prerequisites, 75 host programs and seven production-unit analyses.

2026-11-11 native SMMU continuation:
`out/smmu-native-preflight-20261111-final-v2/receipt.json` passes all four
local prerequisites, 74 host programs and seven production-unit analyses.
`out/smmu-native-local-20261111-final-v2/receipt.json` accepts native SMMU,
shared MMIO guards and staged AArch64 compilation. Explicit real GMU/SQE
ASan/UBSan runs cover actual C++ pool pin/native integration with emulated
hardware. See `smmu-native-backend.md` for identities and limitations.
Complete XNU link remains pending for the combined unhooked native batches.

2026-11-11 native PDC/RSC continuation:
`out/pdc-native-preflight-20261111-final-v1/receipt.json` passes all four
local prerequisites, 73 host programs and seven production-unit analyses.
`out/pdc-native-local-20261111-final-v1/receipt.json` accepts the actual native
PDC/RSC and shared MMIO guards, host sanitizers and staged AArch64 compilation.
See `pdc-native-backend.md` for explicit captured-FDT and real-firmware host
regressions. Full XNU link remains pending for the combined native batch.

2026-11-11 native CM3 continuation:
`out/cm3-native-preflight-20261111-final-v1/receipt.json` passes all four
local prerequisites, 72 host programs and seven production-unit analyses.
`out/cm3-native-local-20261111-final-v1/receipt.json` accepts the actual native
CM3 branch, delayed ACK/PROT_NONE cases and staged AArch64 compilation.
See `cm3-native-backend.md` for shared MMIO regression evidence and limits.
Full XNU link/artifact acceptance remains pending for the combined native batch;
the accepted exit-tail kernel and diagnostic remain pinned and unchanged.

2026-11-10 native thermal continuation:
`out/tsens-native-preflight-20261110-final-v1/receipt.json` passes all four
local prerequisites, 71 host programs and seven production-unit analyses.
`out/tsens-native-local-20261110-final-v1/receipt.json` covers the actual native
TSENS branch and staged-only compilation; `tsens-native-backend.md` records
the independent downstream/FDT and explicit GMU/SQE host runs. This production
batch still needs complete XNU link/artifact acceptance before integrated use.

2026-11-10 native power continuation:
`out/power-native-preflight-20261110-final-v1/receipt.json` passes all four
local prerequisites, 70 host programs and seven production-unit analyses.
`out/power-native-local-20261110-final-v1/receipt.json` additionally accepts
the actual SWEET native adapter and staged-only AArch64 compilation. These
are not a complete XNU link or device execution. The new native power and ARC
access batch still needs full artifact acceptance before integrated use; the
already accepted exit-tail diagnostic below stays pinned and unchanged.

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
The real-kernel local candidate now passes all five packaging steps:
`out/rpmh-tail-real-42835691416-v1/boot-rpmh-tail.img`, 70696960 bytes, SHA256
`b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`.
The `receipt.json` and independent `boot-verification.json` in that directory
pass. It has not been booted; earlier synthetic fixtures are never candidates.

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
