# RPMh Exit-Tail Diagnostic

2026-11-10: the opt-in isolated DXE core now collects software CX/MX requests
inside the **actual `CoreExitBootServices` tail**. This is locally accepted
source and firmware, not a phone-tested collector or a GPU execution candidate.
Default core staging, DarwinBoot, XNU and the running phone are unchanged.

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
