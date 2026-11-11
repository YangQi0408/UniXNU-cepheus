# TZ Runtime Identity Boundary

2026-11-12 source audit and offline tools. No device, SMC, MMIO, boot or firmware execution.
The fixed recovery partition is `TZ.XF.5.2-00209`, but its SHA256 identifies
stored bytes, not the secure image running during the saved false query.
The paired query records establish normal64 inputs and false outputs only.

## Copied-View And Mapping Probe (01:59)

At 2026-11-12 01:59 +08:00, the freestanding C headers
`firmware/cepheus_smem_copied_view.h` and `firmware/cepheus_smem_copy.h`
implement bounded parsing and callback-only double-copy acquisition. No live
mapping, device callback, production caller or GPU admission was added.
Both copies and partial reads remain caller-owned; equality is not atomicity
or authenticated running-image identity. The caller must independently prove
the actual pre-EBS mapping, access permissions and buffer lifetime.

`scripts/test-smem-copied-view.py` passed normally and under `python -O`:
2,411 C/Python differential cases per run, full-corpus ASan/UBSan, output/alias
and pre-dereference guards, mocked copy lifecycle (32 read and 36 mapping
failures), and freestanding AArch64 compilation of both headers. Initial runs
failed with disk quota in `/tmp`; successful reruns used
`out/smem-copy-validation-20261112/tmp`. Capacity coverage includes 512 global
allocations, 84 partitions and item65534 of a declared65535-item table.
These results do not validate a live mapping or actual GPU execution.

`firmware/CepheusSmemMapProbe.c` now supplies a native **mapping diagnostic**, not
a live-copy adapter. It is not linked into a loader and has no production caller.
Prepare accepts only an explicit profile from the identity-bound compiled
Cepheus platform table; the fixed address is a cross-check, never a fallback.
It creates an ExitBootServices event and binds the CPU. Probe consumes one
attempt, checks the bounded HOB chain for exactly one reserved SMEM resource,
rejects conflicting resource/allocation records, obtains the GCD descriptor,
and records all512 page translations plus CPU context before/after. Foreign
or partial mappings, Device/cacheable attributes, unsupported EL/granule/PA
formats, hardware AF/dirty updates and changed CPU state are refused.
Raw partial evidence survives failure and subsequent calls. Close releases the
event before module unload; after EBS it invokes no service. A failed pre-EBS
CloseEvent retains the handle for cleanup without enabling another probe.

The architecture path uses AT S1E1R only at EL1 and preserves PAR_EL1/DAIF
around each query. It does not load SMEM bytes, modify page tables, flush caches,
call SMC or grant access. Stage-1 translation is **not** EL2/XPU/bus-abort proof;
successful mapping evidence alone must not authorize a subsequent raw read.
EFI table pointers and ordinary output RAM disjoint from tables/module state
are trusted caller preconditions, not arbitrary-pointer abort containment.
Output/status is a native diagnostic structure, not a versioned DT wire ABI.

Source inspection explains why several tempting shortcuts were rejected:

- Cepheus `PlatformMemoryMapLib.c` declares SMEM as AddMem/MEM_RES/WC with Normal
  noncacheable Arm attributes. `MemoryInitPeiLib.c` creates its resource HOB and
  identity MMU descriptor, but logs ArmConfigureMmu failure and still returns
  EFI_SUCCESS. Source inclusion therefore does not prove the running map.
- DXE GCD converts EFI_RESOURCE_MEMORY_RESERVED to Reserved, not ordinary
  allocatable system RAM. This SMEM profile produces no allocation HOB there.
- CpuDxe `MemoryAttribute.c` masks GetMemoryAttributes output down to RP/RO/XP;
  it cannot alone establish cache type or physical translation. Its source is
  not a verified running protocol identity.
- `MemoryMapHelperLib` delegates to UEFI Config ShLib; its getter remains
  unaudited. `_XN` platform aliases do not independently prove execute protection.

Final focused receipt `out/smem-copy-final-20261112-v1/receipt.json`, SHA256
`5deace4772c6e477ac0b8574c6a7fbf05575ecce6b56d5ce3afd704944b9e4e6`,
contains six PASS checks (Python parser, C copy/parser and map probe, each
ordinary/-O), with unchanged source and nine reference-source hashes. Native
map-probe AArch64 compile/static analysis and instruction checks pass; host
ASan/UBSan covers512 page faults, PA/cache bits, CPU/EBS changes and HOB/GCD/
profile failures with services/AT mocked. No firmware link, new kernel/CI, phone
operation, SMEM capture or GPU execution occurred. The earlier2407-case result
is historical; the final receipt binds the expanded2411-case script.

Next stage/link one isolated mapping diagnostic from the pinned platform profile
and design a bounded evidence handoff. Do not read SMEM based solely on this
diagnostic, invoke SmemDxe, or repeat PAS/touch tests. Actual independent identity,
resource owner and secure PA/cache/crypto/authentication remain unresolved.

## Independent Metadata Route

### Same-Boot Copied Handoff (02:54)

The mapping diagnostic now has a separate staged DarwinBoot integration and
an XNU receiver, both isolated by `cephsmemmap=1`. A fixed 4576-byte v1 record
in `/chosen/cepheus-smem-map` carries CPU contexts, the resource HOB, GCD fields,
all512 PAR values, partial-failure statuses, capture ticks, source/profile
SHA256 metadata, tree placement and the successful EBS attempt number. The
record contains no callable pointer; GCD image/device values are numeric
diagnostics only. A checksum detects transfer damage, not authenticity.

DarwinBoot's actual bounded ADT parser requires exactly one zero-filled slot
under `/chosen`, exact size, no placeholder flag and no mixed/duplicate profile.
Capture occurs once before the final memory-map phase. It calls the real
prepare/probe/close sequence without console printing. A probe failure keeps
its partial diagnostic; a cleanup failure refuses boot while retaining the
event/module, with Unload able to retry Close. An EBS observed during capture
halts without further firmware services. Successful EBS precedes publication;
the post-EBS path only seals and copies ordinary owned RAM. Map/EBS retries do
not repeat capture. Existing DarwinEnter cache/MMU/TLB teardown remains intact.

After a90-second wait on the isolated diagnostic thread, XNU copies the
unaligned property into a static aligned record, validates it, and emits72 raw
64-byte chunks plus begin/end markers. The last chunk's32 padding bytes are
zero. It does not read SMEM, query SCM, probe GPU registers, or change admission.
The host reconstructs every byte, checks ordering/padding/checksum and the
success predicate independently, retaining complete partial-failure records.
Missing/duplicate/reordered/truncated records and contradictory summaries are
rejected. `HANDOFF_COMPLETE` means a complete diagnostic envelope, not a passed
mapping test. Embedded build hashes are metadata, not running-image attestation;
tree fields and EBS counters are not an authenticated same-boot origin proof.

Local v2 build `out/smem-handoff-firmware-20261112-v2/` linked DarwinBoot in7s.
EFI32256B SHA256
`02badcc35f6cb50c29c5dba412683cdf63796969496313749d6e6b62b340ae8a`.
Native verification decodes4287 instructions across all three executable
sections, checks AT RVA0x35f4 and PAR/DAIF restoration, no SMC/HVC/imports,
and the existing separate DarwinEnter cleanup. Reconstructed staging matches.
Native probe analysis has zero warnings. DarwinBoot retains one baseline
GetMap/Descriptor null warning (baseline has two); no new warning is introduced.
The ignored optional PDB-copy message remains in the saved build log. V1 is
historical; v2 preserves noinline capture/publication symbols for verification.

Focused tests cover actual capture/copy and EBS loop under mocked services/AT,
512 partial page failures, cleanup/EBS refusal, successful attempts1..3, map/
terminal EBS failures,4576 byte checksum guards,1144 resealed word mutations,
4720 actual ADT truncations,18 damaged-EFI/staging rejections, actual XNU helper
to independent host round trip, and relevant SCM/RPMh/SoC/staging regressions.
Both ordinary/-O new checks pass with ASan/UBSan for producer/XNU fixtures.
Portable contract coverage is registered in existing kernel/offline checks;
the new header is copied by both local staging and the kernel workflow.

Final receipt `out/smem-handoff-final-20261112-v2/receipt.json`, SHA256
`a7d08fa4bbcdc431448c1a90dadb04ecda423886872b0db1df16134136683fef`,
contains13 PASS checks. All34 bound source hashes were independently rechecked;
ordinary/-O linked verification records have identical hashes. V1 receipt is
historical; v2 also binds the build log and PE helper dependencies.

"Persistent" here means owned bytes survive EBS into this XNU boot. This adds
no firmware variable, disk write or new log-storage backend. The existing
SWEET console ring/USB route is reused by XNU printf, not by UEFI console;
UDP delivery and ring concurrency are not authenticated or guaranteed. A
fixed90s wait does not establish logger readiness. Original raw logs must be
preserved even when the strict analyzer rejects them.

Full XNU CI `43103322477 @13a975c` and independent native/source verification
passed on2026-11-12. The receiver's fortified copy helper checks x3 against x2,
tails to the inspected pure copy implementation on success and panics on
overflow. The actual chunk call supplies a64-byte object size and selects at
most64 bytes, with final32-byte padding zeroed. No new production call or
security gate was added to fix the verifier's earlier copy-name rejection.

Local candidate `out/smem-map-real-43103322477-v1/boot-smem-map.img` passed all
five packaging steps, nested firmware/FAT equality, actual full-ADT parsing and
cross-receipt identity checks. Its SHA256 is
`005b64524421ab90fa6d2b991d2e8cd298d19aee48e64b0d0c08cc4a1d2e2091`.
Only DarwinBoot changed among197 nested files, not DxeCore; shim/DTB and other
FAT files remain unchanged. These are offline link/package results. No EFI
execution, phone operation, live mapping observation, SMEM content capture,
PAS mutation/query or GPU execution occurred. The user requested stopping
before phone testing; preserve the current phone state. The next hardware
decision is this isolated mapping diagnostic, not a GPU submission test.
Even a live passing diagnostic does not establish EL2/XPU/bus-abort containment
or authorize content acquisition. PAS13=false, CP halted, real ownership,
secure PA/cache, crypto and authentication blockers remain unchanged.

```sh
bash scripts/build-smem-handoff-firmware.sh NEW_BUILD ../unixnu/drivers/DarwinBoot ../uefi ../unixnu/package > NEW_BUILD-build.log 2>&1
python3 scripts/validate-smem-handoff.py --build NEW_BUILD --repository ../unixnu/drivers/DarwinBoot --uefi ../uefi --output NEW_CHECKS
python3 scripts/analyze-smem-handoff-log.py SAVED_XNU_LOG --output NEW_RESULT.json
```

### Standalone Diagnostic Link (02:21)

The mapping probe now has an isolated native driver entry and complete local
EDK2 link. `CepheusSmemMapDriver.c` requires the exact UTF-16 load option
`cephsmemmap=1` including its terminator and exact length; absent, duplicated,
extended or foreign options do not probe. It selects the unique SMEM entry from
the compiled, hash-bound Cepheus platform table, then prepares/probes/closes
once. The diagnostic entry calls the real probe; it is not integrated into
DarwinBoot, any accepted firmware volume or the current phone boot path.

Event-cleanup failure keeps the driver loaded, so no live callback points to
unloaded code; Unload may retry pre-EBS cleanup. After the probe's EBS event,
neither console output nor event cleanup runs. The initial v1 local build used
UefiDebugLibStdErr, whose constructor/destructor creates/closes an implicit EBS
event without propagating errors. That build is historical, not final. V2 uses
BaseDebugLibNull and explicit AsciiPrint instead, removing the implicit event.
AsciiPrint allocates/frees a temporary ordinary-RAM formatting buffer and calls
the UEFI console; this is not a silent or allocation-free diagnostic and does
not promise persistent or USB output. The current source-tree dependencies are
explicit; sibling UEFI and DarwinBoot trees were not changed.

Local build `out/smem-map-firmware-20261112-v2/` completed in six seconds.
`build/SmemMapProbe/RELEASE_CLANGPDB/AARCH64/CepheusSmemMapProbe.efi` is19456B,
SHA256 `5e213732875fb97f7cf5fd8344a8774f4e6bb1781505ea0cb92f585df83d9af1`.
It is PE32+ ARM64/EFI_BOOT_SERVICE_DRIVER with no import table. Its linked
native AT is RVA0x16d8, with PAR/DAIF save and restore checked against original
code bytes; 1889 code instructions decoded. No SMC/HVC/TLBI/cache-maintenance
instructions or additional system-register stores were found in that code.
This limited static check is not a complete indirect-call or bus-abort proof.
Analysis of both actual source files using native build flags reports no
warnings; reconstructed staging bytes and the exact build manifest match.
EDK2's ignored missing optional PDB copy message is retained in the build log;
it does not substitute for a PDB artifact or invalidate the produced EFI link.

`analyze-smem-map-log.py` accepts one saved diagnostic transcript, including
failed/partial probe records only when they are structurally complete. Missing
or reordered pages, duplicates, foreign profile, malformed numbers, incomplete
success or missing terminator are rejected. The complete success verdict is
`MAPPING_DIAGNOSTIC_PASS`, never SMEM/GPU authorization. All access/authentication
and GPU flags stay false. CLI refuses overwriting existing evidence and writes
no result for malformed input. Original source and log bytes stay external.
Unterminated or missing-end partial logs must be preserved as raw evidence,
not treated as successful acquisition.

Final eight-check receipt `out/smem-map-driver-final-20261112-v1/receipt.json`,
SHA256 `2aca4baa325f52bff1f709a0e2ed9ab601a16fd0a74a10cc894cafa4456b6515`,
binds ordinary/-O probe tests, actual driver entry with mocked services/CPU/AT,
strict parser/CLI (518 truncation points), 18 linked/staging verifier rejection
cases, and the two native linked/static-analysis verifications. Source hashes
were unchanged after validation. No full GPU suite or XNU/firmware-volume build
was needed, and no EFI execution, boot package, phone operation, SMEM content
read, PAS query/mutation or GPU execution occurred.

Reproduction from `gpu-work` (new output directories only):

```sh
bash scripts/build-smem-map-firmware.sh NEW_BUILD ../uefi
python3 scripts/verify-smem-map-firmware.py --build NEW_BUILD --uefi ../uefi --output NEW_RECEIPT.json
python3 scripts/test-smem-map-driver.py --uefi ../uefi
python3 scripts/test-smem-map-firmware.py --build NEW_BUILD --uefi ../uefi
python3 scripts/analyze-smem-map-log.py SAVED_LOG --output NEW_RESULT.json
```

Next prepare a versioned same-boot persistent handoff/transport for this evidence,
with the same isolated profile and no extra SCM/PAS queries. Do not schedule a
console-only phone test or assume the iOS USB logger receives UEFI console
output. Real mapping observations are still absent; even a future successful
mapping diagnostic will not alone prove EL2/XPU read permission or safe bus
access. Actual SMEM acquisition and running-image identity remain unverified.
GPU admission remains false and CP halted.

The local SM8150 kernel at commit
`36f369fdb77319a2984a48b89f025ac3e5f97386` supplies a source-level alternative
to repeating PAS7. `drivers/soc/qcom/socinfo.c:752` gets SMEM item469;
`:1206` renders all nonempty version records via `msm_get_images`;
`:1348` declares `images` mode0444 with no store callback; `:1511` registers it
on the soc device. This route does not require changing `select_image`.
Actual recovery/phone availability and its exact sysfs path are unobserved.
The inspected sources have no worktree changes; they are not proven to be the
kernel running on the phone.

The version table format in that source is 32 records of128B, total4096B.
Each record has 75B name/CRM, 20B variant at75, and 33B OEM version at95.
`include/soc/qcom/socinfo.h:179` names item469. The source does not identify
which index is TZ/HYP, so those indices must not be guessed. Matching a label
such as `TZ.XF.5.2-00209` is metadata correlation, not an authenticated digest.

`image_version`, `image_variant`, `image_crm_version` are mode0644 and their
APPS-record setters can write SMEM. `select_image` changes a shared selector.
Do not use those stores for a read-only acquisition. Even the `images` reader
ignores the size returned by `qcom_smem_get` and renders up to32 records into a
PAGE_SIZE buffer. It is not a model for a new unchecked raw-memory parser;
oversized output can truncate, and declared records must be bounded first.
The image table can be mutable, stale or inconsistent with secure execution.

## UEFI Reuse Limits

The local UEFI tree at commit`d8aab0e2c272caeef5329716ff554c08cfebd6bd`
has `AndromedaPkg/Library/SmemSecLib` and an item469 definition. The Cepheus
source memory map names SMEM at`0x86000000/0x200000`; this is a source profile,
not proof of currently accessible runtime RAM or permission to map/read it.

Do not directly route a live collector through the existing parser yet:

- `SmemGetItemPtrGlobal` ignores `Allocated` and uses a 32-bit size+offset sum.
- Partition pointers/table counts are dereferenced before comprehensive
  containing-region checks; private item checks do not bound the entire data
  range before returning it.
- `SmemLibInit` logs NULL context without returning, and ignores the result of
  `SmemEnumeratePartitions`. These source observations are not observed crashes.

No unrelated UEFI fixes or phone path were added in this audit. A future
collector must consume a copied, bounded view; validate header/table/partition
and exact item size; reject duplicate/overlapping/malformed entries; never
allocate SMEM, write the selector, or guess physical fallback addresses.
The offline copied-view parser described below is now implemented, not a GPU gate.

## Implemented Offline Tools

`scripts/analyze-smem-versions.py` parses an explicit saved item469 (exact4096B),
a single copied SMEM region, or the `msm_get_images` sysfs rendering. It has no
device command or physical-memory accessor. A region requires a caller-supplied
physical base; the source Cepheus address is never an automatic fallback.
It validates initialized/version11 or12 headers, partition-table extents,
host pairs/partition boundaries, global allocation flags/ranges, item IDs,
canaries/padding and exact item469 size. It rejects duplicate/overlapping
allocations and does not stop checking after finding the requested item.

Version11 uses the512-entry global TOC; version12 uses the unique global
partition and its SIII item-count info. Cached and uncached global-partition
lists are supported. The strict single-region profile rejects external AUX
regions and cached alignments below32 (kernel/UEFI first-header formulas differ
there), as well as layouts outside its documented initialized bounds. Those
are unsupported layouts, not proof that the actual phone layout is invalid.
No region has yet been collected from the phone, so no live layout is accepted.

All32 raw records preserve field bytes, fixed offsets, hashes and termination/
printability flags. TZ/HYP prefix matches are **label candidates**, with no
assumed index or uniqueness. Non-ASCII raw fields remain visible as escaped
bytes; they do not produce identity claims. Sysfs parsing retains only rendered
records and rejects partial/reordered/duplicate/malformed output and outputs
at least4096B. It cannot detect truncation exactly between complete records,
recover NUL padding or prove absent raw rows are zero. Every result explicitly
keeps running secure identity unauthenticated and GPU admission unchanged.

`scripts/collect-recovery-smem-versions.py` is prepared but **not run on a phone**.
It requires an already-running recovery, serial67eea163, product cepheus, root,
a reported recovery version and an explicit allowlisted `images` path. Paths
are candidate sysfs locations, not a claim of observed availability. It reads
boot_id and mounts, reads the same versions twice, checks identical bytes and
same boot, then runs the rendering parser. Each command has a20-second timeout;
stdout/stderr stream to new host files and even timeout partials are preserved.
Failures do not retry, boot, reboot, switch systems, write sysfs or touch block
devices. A missing/changed/unavailable node is FAIL, not an alternate path guess.
Recovery's own persistent writes remain outside the collector's guarantee.

Local tests use synthetic SMEM buffers and mocked ADB only:21 parser tests
(including256 deterministic mutations) and2 collector tests with12 lifecycle
modes plus pre-command path/output rejection. Ordinary and `python -O` both
pass; the prior14 mapping tests also pass. These two new checks are registered
in the offline validator; the full heavy GPU suite was **not rerun**, and its
previous42-check receipt is historical, not a receipt for these new files.

Final focused receipt `out/smem-version-offline-20261112-v2/receipt.json` has five
successful checks and unchanged source hashes, SHA256
`f2efd6028025bae66c5cb49ece5b8f68374e5ca3d3140d992fab2cd91428ff6d`.
This is tool/mock verification, not acquisition, firmware execution or GPU.
The earlier v1 receipt remains historical: later changes removed an unwarranted
legacy TOC-size lower bound on v12 partition offsets and added CLI/overlap
coverage. v2 accepts the declared v12 partition at4096B without assuming a full
v11 TOC exists; it still bounds the actual208B initialized header.

Offline commands from the `gpu-work` directory:

```sh
python3 scripts/test-smem-versions.py
python3 scripts/test-collect-smem-versions.py
python3 scripts/analyze-smem-versions.py SAVED_ITEM469.bin \
  --format item --output NEW_RESULT.json
python3 scripts/analyze-smem-versions.py SAVED_SMEM_REGION.bin \
  --format region --physical-base CALLER_VERIFIED_BASE --output NEW_RESULT.json
```

Do not invoke the collector merely because a phone is connected. The current
instruction remains offline/preserve state. A future acquisition must first
confirm the already-running environment and accessible node without booting
the rejected Android/recovery images, or prepare an independently checked UEFI
copied-view route. No candidate diagnostic boot image was built in this batch.

## Existing Evidence And Next Decision

The saved recovery dmesg starts around524 seconds and the bounded search found
no TZ/HYP identity or ZAP-success record. Recovery properties report
`qseecomd=running`, which proves neither ZAP authentication nor image identity.
The backup manifest outside the workspace names only a boot partition.
Filename searches of workspace inputs/backups/logs found no HYP/ABL/XBL image
or SMEM version dump; this is a bounded search, not a complete machine inventory.

Next select one independent acquisition route only after its access/lifetime
is verified. The host parser and fixed recovery collector are prepared; actual
recovery node availability and a no-system-switch UEFI capture remain unverified.
The local Cepheus DXE list includes `QcomPkg/Drivers/SmemDxe/SmemDxe.inf`; its
40960B shipped EFI is now a concrete next audit target, SHA256
`4553d85a7cfe19d9f7642822ce02799ac796faf17843391bc75db3b2db7c4a0e`.
The generic protocol header declares a GetFunc but its actual semantics,
registration, side effects and returned-view lifetime were unknown at the end
of the00:47 batch. The subsequent static audit below now establishes its
registration and several reasons not to call it as a read-only collector.
Its inclusion is not an observed running protocol identity.
Do not repeat paired INFO/PAS queries, boot rejected Android images, read
arbitrary secure memory, or change PAS13 admission based on version strings.
Actual owner/secure PA/cache/crypto/authentication remain separate blockers.

## Shipped SmemDxe Audit (01:11)

The exact40960B EFI above has been analyzed with REA/Ghidra12.1.2, then checked
against original file bytes using Capstone and LLVM. The same bytes are embedded
in historical accepted pair boot SHA256
`2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2`,
FFS`F541D663-4A48-40AA-AABF-FF158CCAE34C`. This is **historical packaged
identity**, not evidence that the protocol dispatched or that current RAM is
accessible. The initial connected provider failed with a temporary-directory
quota error; a scoped local REA session using an on-disk TMPDIR completed.
No partial failed analysis is used. The successful session was exported and
closed; there are no surviving audit processes or phone operations.

Confirmed original-image facts (addresses are image-relative RVAs, not live VAs):

- Entry`0x13a4` calls initialization`0x1e60`, then BootServices+`0x148`
  (InstallMultipleProtocolInterfaces) with GUID at`0x8048` and table`0x8078`.
  The GUID is`f4e5c7d0-d239-47cb-aacd-7f66ef763238`; table revision is`0x10001`,
  followed by alloc`0x13f4`, get`0x1420`, ex-alloc`0x1464`, ex-get`0x1478`.
- Get wrapper`0x1420` passes a stack size temporary to`0x1fc8`, copies the
  returned pointer and size to caller outputs, and returns EFI_SUCCESS after
  the delegate returns, including NULL-pointer failures. It does not initialize
  the size temporary first; state/function/item-limit failures in`0x1fc8`
  can return without writing size. Non-NULL output arguments are mandatory.
  Initializing the caller's size alone would not fix that internal temporary.
- The getter has a lazy initialization branch if local state`[0x8130] !=3`.
  Initialization calls`0x23c0` for item3; its original store at`0x2428` ORs the
  version into SMEM base+`0x40+0x1c`, namely base+`0x5c`. Initialization can also
  assert/spin. Therefore a getter call is not unconditionally read-only.
- Initialized v11 selects function-table`0x63c8`, getter`0x25c4`. For item469
  it checks allocated==1 and returns a shared base+offset, not a copied buffer.
  Both nominal lock hooks`0x3538/0x353c` are single RET instructions. The path
  accepting nonzero AUX metadata still returns base+offset; no external-AUX
  interpretation is authorized by this audit.
- V12 selects table`0x6500`, getter`0x2aa4` and scanner`0x2d58`. It refreshes
  mutable **driver-local** partition cache fields, searches the global-host
  record, and returns an uncached/cached shared pointer from`0x2f94`.
  Scanner instruction`0x2e28` explicitly truncates the traversal base to32 bits.
  The RET-only hooks provide no interprocessor snapshot lock or returned lease.
- Target initialization`0x3420` reads two source-selected MMIO words and
  combines them into a target-info pointer; it extracts SMEM base from that
  info. These are static instructions, not observed registers or permission to
  read them. The PE subsystem is EFI_BOOT_SERVICE_DRIVER, not a runtime-service
  lifetime guarantee. No use after ExitBootServices is justified.

This audit **rejects direct GetFunc acquisition for now**. It does not prove
that every initialized successful path changes shared item bytes, nor claim a
complete debug/helper side-effect graph. It establishes enough hazards to
require a separate copied-view route with verified pre-EBS mapping, bounded
access and consistency checks. Version metadata remains unauthenticated even
if that future capture succeeds; PAS13/owner/secure-memory/crypto gates do not
change. No production caller, boot image, kernel or CI job was added.

New public static verifier`scripts/verify-smemdxe-evidence.py` binds12 REA byte
ranges (8code/4data), registered GUID/tables, historical embedded identity and
43 critical instructions. Nine fixed-artifact tests cover foreign EFI/boot/
Evidence, truncated/changed bytes, missing/duplicate selection and LLVM failure.
Ordinary and`python -O` PASS; these private-input tests are not added to CI or
the public GPU offline suite. The full heavy suite/kernel build was not rerun.
The first verifier run rejected the unsigned spelling of a Capstone signed
immediate (`w2,#-0x10000`); the corrected original-bit-pattern check passes.

Private evidence at`../logs/smemdxe-audit-20261112-v1/`:

```text
763634d1708e69857ecb0172af5e4e8c4f8881e5881a9958259eeb17661e3d7f  evidence.json (35 records)
f59797ed5ec9517fc807965d96dc9673a2bd80b1d5784ad9a5769b9c35e2f888  snapshot.json (closed)
0be0aee8dde8e1c55cda73867eeab106a3633d708355972c542808647bb54c55  validation-v2.json
943afac61bbe0b6d88cbeac3946030cc9c16cbdd3393e60cae97690ed0a3a079  validation-v2-opt.json
```

Final-v2 binds the verifier and all five imported helper source hashes; those
hashes were independently rechecked after ordinary/-O tests. Ordinary/-O
semantic receipts match after removing the disposable LLVM listing hash.
Earlier`validation.json` remains historical and does not cover this final
receipt-provenance addition.

Key REA dossiers retained in that bundle:

- Registration:`ev_6873b32a56b9573c3e85a4337b778e0d767a50eb6085bb41668ea04d0c7235a8`
- Get wrapper:`ev_e9f49830d4111feb72cc148b78227ccc16bb7275d3f95f1dded3c596ae17545d`
- Lazy delegate:`ev_5a7ed99099838f610a8b849048c94b40479e6c1a1c8a2ed37458bcb06f8d5083`
- Version setter:`ev_ad4819f6d672d270b5e6f94c495309b1f2ea873ccb468db9c70028a89f9e3b28`
- V12 scan:`ev_5e42ee5b546d28b8bb59d75d16d28e789e0fd64da7484f7d992be07e30661327`

Local workspace-root verification (proprietary inputs stay private):

```sh
python3 gpu-work/scripts/test-smemdxe-evidence.py
python3 gpu-work/scripts/verify-smemdxe-evidence.py \
  --efi upstream-sync/uefi/Platforms/SurfaceDuo1Pkg/Device/xiaomi-cepheus/Binaries/QcomPkg/Drivers/SmemDxe/SmemDxe.efi \
  --bundle logs/smemdxe-audit-20261112-v1/evidence.json \
  --boot gpu-work/out/scm-pair-final-6ce4c7c/boot-scm-pair.img \
  --output NEW_STATIC_RECEIPT.json
```

Next inspect the actual Cepheus platform memory-map installation and pre-EBS
mapping/lifetime boundary, then prepare one bounded copied-view route only if
that evidence supports it. Do not use target-info MMIO or a guessed physical
base as a fallback; no phone change or repeated PAS/touch test is needed now.

## Source Digests

```text
74a954ed41e97ad20b28cb868e9161e2781f524013a88f347e45a0d9f29653a0  kernel/drivers/soc/qcom/socinfo.c
517b7b3b6ac18759ce826447d9e93b3cba24b638008faa874b1ca29229335878  kernel/drivers/soc/qcom/smem.c
51e3d7c4268f497d0344547221186d40565d012d31ff9d47a9ee6f9e45a24eee  kernel/include/soc/qcom/socinfo.h
018b6965cd4ed8bf619a2ed21b0aad9b5efc093a77cf23d104af542b18606694  uefi/Platforms/AndromedaPkg/Library/SmemSecLib/smem.c
de68306a21ed82ad76280559b2b09ed970ea9787f5b7e5994a3d697dd285f7a6  uefi/Platforms/AndromedaPkg/Library/SmemSecLib/socinfo.h
```

Kernel root:`/home/yangqi/android/kernel/xiaomi/sm8150`.
UEFI root:`../upstream-sync/uefi`. Sources were read locally, not downloaded.
