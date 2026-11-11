# Local build workflow

2026-11-11: CP execution admission identity/native wrapper passes focused and
full local preflight on accepted production source. Receipt is
`/tmp/gpll0-admission-final-v2/receipt.json`, SHA256
`17e29e90417fb50bdddf6751937838effbe83dbe977de9e1cd8d7ce79aecd60d`.
It binds parent `a538e53`; the later test-fixture/document-only cleanup did not
change production bytes, so this is not presented as a new `f944723` receipt.
Original GMU/SQE GX SMMU modes0..24 and ring modes25..31 pass ASan/UBSan with
emulated MMIO/ACK/IOKit/owners. CI42959844615 at `a538e53` succeeded; its
downloaded artifact is17563504 bytes, SHA256
`0758ee753fa10f0c366a9b29bac339816efdf7f6ba884a89367533ee7f5df229`.
It links ring/CP/ZAP but not the subsequent admission. Admission is committed
as `f944723`; CI42964201439 failed at staging because the workflow omitted the
two admission headers. The copy manifest and a regression are fixed; local
staging-fix acceptance is 164 steps PASS, receipt SHA256
`d222669e46f08e9482e9e9318bd5d7a7665dcc580ac3976a64edfbd10c41f614`.
See `cp-execution-admission.md`. No phone operation or GPU execution.

2026-11-11: native CP/atomic pool sealing committed/pushed as78d34a8 after
frozen153-step focused and four-step83-host/seven-analysis acceptance. Single
replacement complete XNU CI42939546189 is in progress, not yet an accepted
artifact. Committed source is archived at `out/ci-source-78d34a8/` so later
ring edits do not invalidate independent kernel/source comparison. No phone
operation or GPU execution. See `cp-native-backend.md` for exact receipts;
`cp-ring-native-backend.md` describes the subsequent uncommitted batch.

2026-11-11: CI42907357305 @31215d3 accepted using archived-source scripts:
106 core/35 memory/no unresolved, touch/PA64/SCM/tail/RPMh/idle pass. The
native config/HFI kernel excludes later ceaa736 CM3/QMP and new GX edits.
Fresh GX140-check focused/four-step83-host local acceptance passes. Exact identities and
hardware/owner limits: `gx-native-backend.md`. No image packaging or phone IO.

The current SWEET XNU 8796 build uses the pinned macOS builder. Firmware
compilation, patch preparation, host regressions, FV reconstruction, FAT/boot
packaging and independent artifact verification run locally.

## Kernel preflight

2026-11-11 05:36: prior OOB/query/pin kernel CI `42900020339 @471e223`
is independently accepted (101 core/35 memory symbols, no unresolved,
17492488 bytes, SHA256
`98b66afd4039e0fc64f208afbb8a88a6ba1a51fc2f9bf76a90a102424b55f545`).
New native DB/fuse configuration and same-session HFI boot/DCVS/START batch
passes focused final-v2 (135 checks) and four-step preflight final-v2 (82 host).
Both completed with unchanged source hashes. Original-firmware actual-pool
24-mode ASan/UBSan chain passes; hardware/global owner remain emulated.
See `hfi-native-backend.md` for receipts and the bin1 QMP ordering gap.
The new batch still needs its own full XNU link; no phone boot or GPU execution.

2026-11-11 04:53: fixed native HFI query and actual six-buffer pool pin/seal
pass focused final-v4 and production preflight final-v3 (four steps, 81 host,
seven analyses). Subsequent actual SMMU/upload/CM3/HFI + actual pool nine-case
ASan/UBSan integration passes using explicit local original GMU/SQE files.
See `hfi-native-backend.md` for hashes and exact mock/runtime limitations.
Only test additions followed those production receipts; complete XNU link is
next. HFI access `3b0db57` is already committed/pushed, not pending as below.
No phone operation, packaged replacement or GPU/Metal execution.

2026-11-11 04:26: HFI word/link checks accepted locally, focused final-v3
SHA256 `13379064a48a2a15c9a9a0c8dbd9a3089df648822ae60d8225ce7f49b3f8f340`;
full preflight final-v2 SHA256
`01f9d66734f3d429744d296eaa6b6f2693c65671d1913ba57e8593d587370482`,
four prerequisites/80 host/seven analyses, unchanged source hashes. Explicit
real GMU/SQE mixed C/C++ ASan/UBSan chain passes clean. Older whole-chain
exit-zero logs contain invalid-enum reports and are not sanitizer-clean
acceptance; raw integer admission and terminal UBSan checks fix that bridge.
See `hfi-access-lifetime.md`. Still no full XNU link for HFI/OOB or phone GPU.

2026-11-11 04:10: combined parent/QMP CI `42886831693 @8c539ca` completed
SUCCESS at 04:03:13 local time. Independent archived-source acceptance PASS:
17473904-byte kernel SHA256
`b150954326355d39ab71da0461521103a44cdd53d013a8b996985dbf34aecdfa`,
98 core/30 memory entries, no unresolved symbols, touch/PA64/SCM/tail PASS.
Receipt SHA256 `345c2f2b8dc6fbd79a8b5f963ef98b84007d1f1b0e9889439fde4f67f1dcf313`;
actual linked RPMh and idle receipts PASS, respectively SHA256
`2516428bd9c5b64c5e9250746d5d46584e206abe088f4008d687ef32402b6e39` and
`5009b78d65faed23d5976f6433ac350335b6e2cb0e437ce208d160265b67f841`.
No runtime or packaged image. OOB follow-on `ee105f6` is committed/pushed but
not in that kernel. HFI word/identity/lifetime checks are undergoing focused
acceptance with explicit real GMU/SQE mixed C/C++ ASan/UBSan and full local
preflight. No production resource owner, GPU/Metal execution or phone boot.

2026-11-11 03:54: native OOB0 uses the actual private CM3 HFI_READY record,
retained mappings/config/version, real RPMh/idle scope and fixed OOB bits.
Focused final-v3 PASS SHA256 `afdeff4a251699ca592f712d9b4373e15414fb72a431ec24eb66078db3d2b065`;
full final-v3 preflight PASS SHA256 `2cde8885d30cb158adefe4024a36754d388abf10ed54466a918369e595800752`,
four prerequisites/79 host/seven analyses; unchanged recorded source hashes.
Earlier powered-down peer-fixture final-v2 FAIL remains archived. See
`oob-native-backend.md`; this follow-on still needs full XNU link acceptance.
Exactly one QMP+parent run `42886831693 @8c539ca` was dispatched at 03:35:40;
all prerequisites pass and complete XNU is building. It excludes OOB follow-on.
No phone operation, new packaged image or GPU/Metal execution.

2026-11-11 update: GPLL0 CI `42881254237 @d5bf1dd` completed SUCCESS and
independent archived-source acceptance PASS. Kernel 17473248 bytes, SHA256
`3dd662d8d21c6a1d35d01e5af06793e7c8e1d13c0f1f689c4db581c9ea88cf10`;
97 core/30 memory entries, no unresolved, touch/PA64/native SCM/tail PASS.
Receipt SHA256 `3222babe60b28e6b238ae2ae8d234eee1a771f6a35f9a684bec2cbd82b9e297f`.
Actual linked RPMh/idle receipts PASS; separate GPLL0 instruction inspection
is manual, not a general CFG verifier. See `gpll0-scope-coordination.md`.

Native power-parent/private counter binding is committed/pushed as `6e24284`.
Native QMP focused and complete final-v3 local preflight PASS, with unchanged
recorded source hashes: 78 host/seven production analyses/four prerequisites,
preflight SHA256 `fa930323fc877ec9a999cc79207d8fcb86a80d0999f0c4fc728ad4dafe6d1086`.
See `qmp-native-backend.md` for focused receipt and failed earlier run limits.
Neither later batch is in CI42881254237; one subsequent combined full XNU
link is needed. Firmware/patches/host/FV/boot/verification stay local. There
is no production resource owner/startup or GPU/Metal execution.

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
archived. GPLL0 is NOT in CI42874717231; its later full-link acceptance is
CI42881254237 above. No phone operation or GPU execution.

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

## SMEM Mapping Handoff

The isolated `cephsmemmap=1` package replaces DarwinBoot but not DxeCore,
preserves the accepted shim/DTB and other FAT files, and adds one zero-filled
4576-byte `/chosen/cepheus-smem-map` property. It does not collect SMEM content,
call SCM or execute the GPU. A completed full-kernel CI receipt must include
`verify-ci-scm-pair.py --smem-map`; packaging independently checks the native
receiver again and matches every production XNU source hash. A marker-only
synthetic kernel is refused. Assertion-based legacy packaging requires normal
Python; `-O` is explicitly refused for the new profile.

After downloading and independently verifying the real kernel:

```sh
python3 scripts/build-scm-pair-local.py \
  --kernel PATH_TO_VERIFIED_KERNEL \
  --kernel-receipt PATH_TO_VERIFIED_KERNEL_RECEIPT \
  --output out/smem-map-NEW --smem-map \
  --firmware-build out/smem-handoff-firmware-20261112-v2 \
  --container cepheus-build:latest
```

The local container needs Python 3.11+, mtools and LLVM objdump; it runs with
network disabled and the invoking UID/GID. Firmware native verification stays
on the host, followed by baseline-reproduced FV rebuilding, boot packaging and
independent nested-FV/EFI/FAT equality verification. A final host-RAM execution
of the actual staged DarwinBoot ADT parser verifies the complete packaged tree,
including its real WindowSize.h, unique SMEM slot and unchanged 3GiB window.
A PASS is offline package
acceptance, not a boot, mapping-access authorization or GPU result.

On 2026-11-12, full XNU CI `43103322477 @13a975c` and independent receipt
`out/kernel-verification-ci-43103322477-smem-v1.json` passed. The real kernel is
`../inputs/ci-43103322477/cepheus-touch-kernel/kernel.macho`, 17597848 bytes,
SHA256 `6521a88f9d576445a105fc07a8004a97dd6d78528afb7e724e66abe6fcce09f6`.
The native receiver, outlined validation helper, fortified copy size guard and
copy implementation passed inspection, alongside touch and pmap cache checks.

The genuine local candidate passed all five packaging steps and cross-receipt
identity checks: `out/smem-map-real-43103322477-v1/boot-smem-map.img`, 70684672
bytes, SHA256
`005b64524421ab90fa6d2b991d2e8cd298d19aee48e64b0d0c08cc4a1d2e2091`.
Only DarwinBoot changed among 197 nested firmware files; DxeCore, shim, DTB and
other FAT files remain unchanged. The actual parser accepted the complete
17388-byte ADT, zero 4576-byte slot at offset1372 and unchanged 3GiB window.
This candidate has not been booted. Do not use either existing SCM-pair or
RPMh-tail device runner for this distinct profile. Phone testing and any SMEM
content acquisition require a separate explicit decision; no GPU execution or
PAS admission has been established.

The dedicated `scripts/run-smem-map-device.py` is now prepared and validated
offline for this exact candidate. It is not an offline build command: invoking
its lifecycle issues one temporary fastboot boot, so it remains unused until
the separate hardware decision. It checks the image again after capture READY,
accepts only one Cepheus device, retains raw logs, and has no flash/reboot/retry
path. Capture defaults to300 seconds (allowed300..600).

The runner independently reconstructs the4576-byte record and checks its three
embedded source hashes against the accepted candidate. A complete failed
mapping diagnostic is retained as `result=PASS`, `evidence_complete=true`,
`mapping_diagnostic_pass=false`: PASS here means evidence completeness only.
Incomplete/rejected collection is FAIL with an unknown mapping result. Neither
case authorizes SMEM reads, authenticates a secure identity or executes GPU.
Focused normal/`python -O` lifecycle, integration and host-adapted actual
producer/XNU round-trip checks passed; the integration suite has one optional
reference-source skip. Receipt:
`out/smem-device-runner-offline-20261112-v1/receipt.json`, SHA256
`a210e073c232741233512e5e0ba6c3b96a3de6fe56763ddca8897bff9b635e74`.
No full kernel rebuild or device operation was needed for this host-tool change.

## Full Linux XNU

On 2026-11-09 the host dependency check reported missing `tcsh`, `unifdef`,
`mformat` and `mcopy`. No configured `mig`, `iig`, Mach-O ld64 or XNU-specific
Clang toolchain was found. The inspected UniXNU Linux tooling at commit
`350ddc2a23dcaea7d74d8a469259c2efcec3e4a9` targets XNU 10063/iOS 17; the
accepted Cepheus kernel uses XNU 8796/SWEET. Its toolchain and SDK preparation
need adaptation and an actual compile/link check before a local kernel can
replace the current macOS artifact. The upstream installer also writes shims
to `/usr/bin`; it has not been executed or installed on this host.

On 2026-11-12 the host does provide `ld64.lld`. That does not provide the missing
MIG/IIG or a validated full XNU8796 toolchain. The local build container supplies
mtools; no upstream installer has been run.

Full XNU remains in CI for this candidate. A future local migration should use
an isolated prefix/container and retain the current XNU version, board config,
patch order, SDK ABI and compiled-artifact checks.
