# RPMh Exit-Tail Diagnostic

2026-11-10: the opt-in isolated DXE core now collects software CX/MX requests
inside the **actual `CoreExitBootServices` tail**. This is locally accepted
source and firmware, not a phone-tested collector or a GPU execution candidate.
Default core staging, DarwinBoot, XNU and the running phone are unchanged.

## Integration Update

22:59 correction: matching library names and generated PCDs did not cover
compile-time platform behavior. The v2 platform core omitted
`__DEPRECATED_AARCH64_4K_RUNTIME_GRANULARITY` and used 64 KiB runtime allocation
instead of the accepted core's 4 KiB. It is superseded and rejected by the
updated validator. `RpmhTailCore.dsc` now preserves the accepted platform's
complete extra compiler flags and core link alignment. The validator compares
eight compiler/assembler/link/GenFw flag groups, in order, for the core and all
40 linked libraries, including library-specific flags. Default DSC is unchanged.

Accepted replacement: `out/rpmh-tail-core-platform-20261110-v3/`, 369664 bytes,
SHA256 `22c81124c3e8513b1205001900481d463e2043ab1ce114f1071596508ed88ccf`.
`out/rpmh-tail-core-platform-accepted-20261110-v4.json` passes the actual
collector/EBS/loader, O0/O2 sanitizers, native analysis/assembly, generated PCDs,
LZMA/CRC registration and exact core/library option checks. The first validator
attempt requested a nonexistent `SLINK_FLAGS` make variable and failed; that
test harness error was corrected, not counted as an accepted run.
Replacement FD: `out/rpmh-tail-fv-platform-20261110-v2/scm-pair.fd`, 3145728 bytes,
SHA256 `f5b685a13a1cd1c939f512ed51204bdd35ccdd578e520af5af4bec45fb133da5`.
Independent synthetic-kernel packaging and rejection tests pass. All older
core/FV references below are historical, not current phone candidates.

Full XNU CI **42835691416 @2bf48ab** completed successfully. Downloaded kernel
is 17467744 bytes, SHA256
`97df727b22a19ee58ac0a2e83156dc4bedb2477eb3a09b4c93f26786d05a1ca8`;
`out/kernel-verification-ci-42835691416.json` independently passes 86 core and
22 memory symbols, no unresolved symbols, staged source identity, touch, PA64,
native SCM capture and tail helper/caller/ABI checks. No duplicate CI was run.
Real-kernel packaging is next; no phone operation or GPU execution has occurred.

22:50 correction: the earlier 422400-byte isolated core lacks the accepted
boot core's LZMA/CRC registration libraries and uses different RELEASE PCDs.
It remains host/isolated evidence and must not be used as a phone replacement.
The tail build now selects `firmware/RpmhTailCore.dsc`; default bookkeeping
builds retain `PoolLifetimeCore.dsc` unchanged. Full local build
`out/rpmh-tail-core-platform-20261110-v2/` is 370176 bytes, SHA256
`c41e3e863f6762f48fcdb2ab602b4499f54ebc67e8f6b91a7a590d6df5d3ef90`.
`out/rpmh-tail-core-platform-accepted-20261110-v2.json` passes the actual
collector/EBS/loader chain, O0/O2 sanitizers, native analysis and assembly,
and additionally checks the exact accepted core hash, equal boot-library set,
equal generated typed PCD values/constructor sequence and linked LZMA/CRC
handlers. This checks configuration, not phone boot execution.

The replacement FD is now `out/rpmh-tail-fv-platform-20261110-v1/scm-pair.fd`,
3145728 bytes, SHA256
`53467f210a7a3bf90a921fc84062d4528b49e768bb1de28087e093d2fef82a77`.
Its independent synthetic-kernel packaging/rejection fixture passes; the old
FD below is superseded. Full XNU CI **42835691416 @2bf48ab** is in progress,
with source/toolchain/native prerequisites passed and full compile running.
Track that run; no duplicate build is needed for this firmware-only correction.
Final preflight is `out/rpmh-tail-preflight-20261110-final/receipt.json` PASS.
The running accepted phone is untouched. No real diagnostic boot exists yet.

2026-11-10: the opt-in DarwinBoot adapter, shared core protocol and isolated
XNU receiver are now integrated in source. This supersedes the earlier
no-consumer statement below; the running phone still has not changed.

DarwinBoot prepares/binds before its final map and copies only the core-owned
record after successful EBS. A separate 1128-byte transport envelope preserves
bind/copy failures and the EBS attempt count, exports no firmware pointers,
and uses a zero `/chosen/cepheus-rpmh-tail` slot with `cephrpmhtail=1` only.
The receiver waits 90 seconds, validates an aligned copy, prints 18 raw chunks
and runs the existing read-only, aperture-gated GPU power/clock probe. It
performs no new SCM query, resource vote, PAS operation or CP submission.

Full local loader build `out/rpmh-tail-loader-20261110-v3/` is accepted by
`out/rpmh-tail-loader-test-20261110-v7.json`: 29696 bytes, SHA256
`e0563bc54f783e857191d2a594855c08fee8ed38475b8d206e223ce90fdea7fd`.
Tail-only checks reject missing successful file-read output and null/out-of-range
ADT inputs. Native loader analysis retains one existing map-descriptor warning,
compared with two baseline warnings; this is not a zero-warning claim.
The shared-protocol core build retains the accepted 422400-byte binary hash.
`out/rpmh-tail-loader-core-test-20261110-v2.json` accepts the actual
loader -> Core Bind -> actual EBS -> Core Copy chain at O0/O2 with sanitizers.

`out/rpmh-tail-fv-20261110-v2/verification.json` accepts a 3145728-byte FD,
SHA256 `51f56f1eedbf69be8b5e33d6621cba91c84b38a88e3d6c2398bcc2853b2f3f37`.
Only DarwinBoot and DXE Core change among 197 inner files; the baseline
reproduces and non-PE sections are unchanged. End-to-end packaging with a
synthetic kernel, independent nested-FV verification and rejection cases pass;
that fixture is never a boot candidate. Default pair packaging also passes.

The actual XNU helper and independent Python raw-envelope analyzer pass O0/O2
ASan/UBSan fixtures for unaligned/missing DT, every corrupted byte, collection
and transport errors, missing/duplicate/reordered chunks, final padding and
contradictory summaries. `out/rpmh-tail-preflight-20261110-v1/receipt.json`
passes all four local prerequisites, 69 host programs and seven production-unit
host analyses. A new complete XNU compile/link and its independent artifact
acceptance are still required before a real candidate can be packaged.

No phone operation has occurred. The next hardware point is one consolidated
exit-tail diagnostic boot, not a GPU execution test. Missing global resource
ownership, PAS13/authentication and SMMU admission are not solved by this ABI.

## Boundary

The hook runs after successful memory-map termination, all EBS notifications,
memory-protection callback, debug/CPU interrupt disable, system-table CRC,
boot-services zeroing, `gBS=NULL` and `AtRuntime=true`. Failed EBS attempts do
not capture; a successful tail captures once and records the attempt count.

Before EBS, a core-owned protocol accepts three copied image bases, not caller
rail addresses. Full pinned RPMh, patched DAL and CmdDb relocated headers/text
and the actual DAL function pointers must match. Only the identity-bound CmdDb
`cx.lvl`/`mx.lvl` queries execute at bind, with DAIF restored. The tail calls no
firmware methods. Identity generation omits the old core artifact deliberately:
this collector is compiled into a new core, not the old pinned core.

During capture, local DAIF is saved/masked/restored and actual core pool/map
mutation paths refuse changes and latch failure. EL1, MMU/cache, counter
frequency, monotonic timing, half-second limit, at most 65536 probes and the
actual allocation serial are checked. Final timing/frequency/serial uncertainty
clears success; failed records preserve raw timestamps, including backward time,
and zero all request payloads. Images are read in bounded 256-byte blocks but
every expected byte/relocated word is still compared exactly.

The current `gMemoryMap` is captured into bounded core BSS. Every dynamic node
must belong to a page returned by the **actual `AllocateMemoryMapEntry` path**,
at its real node alignment; static nodes must be actual `mMapStack` slots.
Page-free/attribute conversion invalidates recorded descriptor-page extents.
128 extents, overlap/overflow failure, node origin/FromPages, backlinks,
page alignment and nonoverlapping physical ranges are enforced. It never
follows an arbitrary link just because a CPU translation probe succeeds.

Reads probe stage-1 translation before and after loads with `AT S1E1R`, ISB and
PAR_EL1: identity physical address and exact Normal WB encoding are required.
Data also requires current BootServicesData/WB/no-RP descriptors. DAL payloads
must bind to an actual live type-4 allocation at `payload-4`, with exact core
bytes `payload_bytes+4`, generation and real allocator header/alignment/tail.
Freed, moved, reused, malformed or overlapping objects are rejected.

The separate 1080-byte phase2 ABI has three identity bits, explicit local
boundary bits and nested/outer checksums. The old 1016-byte phase1 validator and
its pre-EBS-only admission remain unchanged. The copy method returns the
core-owned record after EBS; no firmware object pointers are exported.

## Local Acceptance

Full local RELEASE CLANGPDB AArch64 compile/link:
`out/rpmh-tail-core-20261110-v5/`, DxeCore.efi **422400 bytes**, SHA256
`b7a97cd5239d8cbc5f8a5efddd315e22145aa0d6459ea0c2bd44bd8db182a59e`.
Final receipt `out/rpmh-tail-test-20261110-accepted.json`, SHA256
`2b698d8b81a8882a10ee891a661fd0cc344b1c9e3a8e7e9e3674e18fb3876a2e`.

Actual staged Pool.c, `CoreConvertPagesEx`, `AllocateMemoryMapEntry`, map and
collector execute in O0/O2 ASan/UBSan/leak fixtures. All 1834 translation
boundaries fail independently and stop later access; PROT_NONE unknown nodes
fail provenance before dereferencing. Cases cover moved/freed pools, corrupt
maps/identity/ABI, real descriptor-page attribute invalidation, extent
saturation, allocation/free/page-map freezing and late backward/expired time,
frequency, allocation-serial and rejected-mutation changes.

The actual staged `CoreExitBootServices` executes with substituted external
services: invalid-map retry, callback free/move of the resource array, changed
CX request, system-table cleanup and single capture pass. Register/translation
and CmdDb invocation are mocked only in host tests, not production. Complete
native Pool static analysis has no warnings; Page retains exactly the pinned
baseline's two dead stores. Native assembly verifies AT/ISB/PAR, DAIF
mask/restore and real main callers. The receipt binds every staged core file,
fixtures/tooling, DSC, manifest, compiler response, map, log and linked artifact.

Default pool source/build regression, old phase1 snapshot/ADT and SCM-pair
regressions pass. Earlier v1/v2 exceeded the probe budget, v4 failed a newly
strict stage anchor, and intermediate acceptance scripts failed fixture/native
include or LTO-symbol assumptions. Those results are historical, not accepted.

```sh
bash scripts/build-pool-lifetime-core.sh out/NEW --rpmh-tail
python3 scripts/test-rpmh-tail-core.py --firmware-build out/NEW --output out/NEW-receipt.json
```

## Hardware Gap

Local CPU masking and allocator hooks are **not** global AP/EL2/secure/runtime/
DMA writer exclusion. AT is not exception containment, an XPU/EL2 permission
proof or protection against a foreign writer changing mappings after probing.
Untracked allocation/permission paths and global resource writers remain open.
Host timing is not measured boot-time cost, and synthetic translations are not
observations of this phone. The protocol is not a hostile-caller security API.

No DarwinBoot consumer, ADT tail property, core firmware-volume replacement,
boot package or phone validation exists for this batch. No complete inherited
ARC/BCM ledger, hardware ACK or production XNU owner follows from this record.
Do not pass it to `owner.validate` or initialize current hardware votes from it.

Priority now shifts from adding offline cases to a minimally scoped, locally
built hardware-diagnostic integration, followed by one consolidated phone
test when ready. The running accepted phone is retained until then; no repeated
touch/SCM/memory rounds are requested. An exit-tail diagnostic test still would
not be GPU execution. Real power/clock provenance, writer coordination, PAS13
support/authentication, secure PA/crypto and SMMU admission must be resolved
before the first scratch/fence submission. No date for that first submission
has been established. PAS13=false, supported=false rejection and CP halt stay;
scratch/fence and Metal have never executed.
