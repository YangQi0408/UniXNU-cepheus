# Native GX Core Subset

`cepheus_gpu_gx_core_native` maps exactly GPU `0x2c00000/0x40000` after
actual native HFI query, boot, DCVS and START, with the same retained RPMh
token and actual OOB0 HELD. The sealed six-buffer publication, native CM3,
SMMU, upload, config, QMP/bin1 and their live access paths remain prerequisites.
No caller ready bit, GPU VA, register, value or firmware ACK is accepted.

The copied external owner must reserve the fixed profile's GMEM range and
protect real GX/SPTP/fence/power/thermal plus every global writer. The singleton
does not implement this owner. Callbacks are boot-lifetime, non-sleeping and
non-reentrant under GPLL0 serialization; the existing GPLL0 -> RPMh -> idle
order remains. A native stage record is not resource ownership.

Only the existing 23-entry A640v2 UCHE/GMEM/ROQ/UBWC plan executes. Every
individual GPU load/store is enclosed by live HFI/owner/power/fence checks,
including each halt/fault read and each configuration readback. CP halt bit1
must remain set, faults clear. Mapping null/alignment/VA overflow is refused
before GPU dereference, with a post-map check even on null. Counts describe
attempted stores, not execution. Late final callback failure clears success.

An admitted owner remains retained even on pre-map loss because prior HFI/OOB
is already live. Failed acquired attempts mark the sealed pool uncertain.
Missing predecessor/token refusal leaves output unchanged and makes no attempt;
failed begin must unwind its own partial acquisition, without clearing HFI.
There is no retry after an attempted acquisition, unmap, resource lowering,
fence opening, GX wake, CP unhalt, trust-control write or submission.

This is NOT complete GX initialization. HWCG/GBIF/perfcounter/LLCC, actual GX
SMMU/CP workspace publication, security authentication and the real resource
owner are still separate. The vendor orders HWCG before GBIF before core;
future combined startup must preserve that order rather than treating this
isolated subset as the first stage. The native subset has no startup caller.

## Source-Bound Acceptance

Fresh focused140-check PASS:
`out/gx-native-local-20261111-final-v1/receipt.json`, SHA256
`b7a6b8c48dd5e095e1327412ad35a8cd73480c02b9b9e6bfe8110e609cb47cbe`.
Fresh four-step/83-host full local preflight PASS:
`out/gx-native-preflight-20261111-final-v1/receipt.json`, SHA256
`5b2c45c8f063a787ff407c84feac6b3086be508be0bb6b24d1e94f78391a57b9`.
Both source hash sets matched after completion. Original-firmware whole
memory-chain log SHA256
`eed60e35878fe04a60b8602aa8ad27c7512f33d187d968f419f8207d27427dba`.
49 actual-pool modes plus older whole-chain cases pass, no sanitizer reports.
Optional ZAP artifact was not supplied; intentional quarantine leaks are not
checked. The evidence remains host/native-codegen, NOT hardware execution.

Focused synthetic-upstream fixture passes 2424 owner and 1616 admitted scope
PROT_NONE points, fixed writes/readback, final loss, map loss, copied callback
mutation, reentry, halt/fault/token loss. This fixture does not run upstream
backends. A separate actual C/C++ SMMU/upload/CM3/HFI/OOB/pool chain passes
modes0..48 with explicit original GMU firmware, including 18 GX cases31..48.
MMIO, firmware ACKs, global owner, idle and IOKit are emulated. Its first run
failed because a corruption fixture XORed on every callback and could restore
the expected word; the revised fixture continuously forces zero. Failure log
is preserved in `out/gx-native-actual-pool-20261111-v1/mode-42.log`.
The 49-mode v2 passed before the private header's explicit memory include was
added; the fresh focused/full acceptance above supersedes that partial
compile evidence. GPU and Metal have never executed.

## Accepted Preceding Kernel

CI42907357305 @31215d3 succeeded and archived-source independent validation
passes 106 core/35 memory symbols, no unresolved, touch/64-bit PA/SCM/tail.
Kernel17509608B SHA256
`6a27ecbfa46f8e674f8220e83104c04a530579b53c3f33380b5263e5cda3fcd2`.
`out/kernel-verification-ci-42907357305.json` SHA256
`29d69bf126530befab99ad5492ba32e00744e0f9ed2ef9338041525e61ee05f6`.
Actual linked RPMh receipt SHA256
`b5dc7eae91813beae06862faa32bd5041bd600603d93899af80c7f6ac778384b`;
idle receipt SHA256
`9eff12008aa11bb18bca7340d19cb49738f321154f2a37b038727f1c5585a551`.
This kernel includes native config/boot/DCVS/START, NOT the later ceaa736
pre-cold CM3/QMP changes or this GX backend. No boot image was packaged or
phone operation performed. PAS13=false/supported=false and CP halt remain.
