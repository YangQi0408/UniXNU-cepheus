# TZ Runtime Identity Boundary

2026-11-12 source audit and offline tools. No device, SMC, MMIO, boot or firmware execution.
The fixed recovery partition is `TZ.XF.5.2-00209`, but its SHA256 identifies
stored bytes, not the secure image running during the saved false query.
The paired query records establish normal64 inputs and false outputs only.

## Independent Metadata Route

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
registration, side effects and returned-view lifetime have not been inspected.
Do not call it based only on the header or filename. Its inclusion is not an
observed running protocol identity. No new REA session or device was started.
Do not repeat paired INFO/PAS queries, boot rejected Android images, read
arbitrary secure memory, or change PAS13 admission based on version strings.
Actual owner/secure PA/cache/crypto/authentication remain separate blockers.

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
