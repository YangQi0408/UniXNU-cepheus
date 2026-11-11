# TZ Runtime Identity Boundary

2026-11-12 source audit only. No device, SMC, MMIO, boot or firmware execution.
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
An offline copied-view parser is the next implementation step, not a GPU gate.

## Existing Evidence And Next Decision

The saved recovery dmesg starts around524 seconds and the bounded search found
no TZ/HYP identity or ZAP-success record. Recovery properties report
`qseecomd=running`, which proves neither ZAP authentication nor image identity.
The backup manifest outside the workspace names only a boot partition.
Filename searches of workspace inputs/backups/logs found no HYP/ABL/XBL image
or SMEM version dump; this is a bounded search, not a complete machine inventory.

Next prepare the bounded host parser and its rejection tests, then select one
new independent acquisition route only after its access/lifetime is verified.
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
