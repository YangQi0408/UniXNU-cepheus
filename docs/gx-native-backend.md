# Native GX Core Subset

## HWCG And GBIF Follow-On

The accepted subset below was committed/pushed as36c94c3. Single full XNU
CI42915818943 is running at that commit; it excludes this later setup entry.
`cepheus_gpu_gx_setup_native` shares the exact same singleton, copied owner,
map and live HFI/OOB/pool prerequisites, but runs fixed HWCG -> GBIF -> core.
Cold success is83 attempted writes:55 HWCG,5 GBIF,23 core. Already-on HWCG
preserves the source early-out after3 GMU-AO stores; total31 writes.

Only the source's SP-clock-off HWCG interval masks SP_CLK_OFF bit4 from the
bus gate. SPT power bit2, GX power/clock bits6/7, fence, fault, CP halt and
all ownership/publication guards remain required before/after accesses.
That interval begins before the attempted gate store because it may land.
After the enable/readback it closes before the top HWCG store. Any loss leaves
the clock/owner/backing retained; no recovery write, CP start or security bypass.
This adds no perfcounter/LLCC/GX SMMU/CP publication/global owner/startup caller.

Focused actual adapter with synthetic upstream records passes exhaustive
cold8790 owner/5844 admitted scope and early-out3318/2204 PROT_NONE points.
Actual C/C++ memory-pool chain57 modes0..56 passes; modes49..56 add native
setup success/early-out, clock-off revocation, table/GMU readback corruption,
final owner loss and GX/fence loss while SP is gated. Hardware/ACK/owner/idle/
IOKit remain emulated. Source audit independently matches49 HWCG and5 GBIF
entries and vendor order/status bits at pin36f369f; no FDT/hardware proof.
`out/gx-setup-vendor-20261111-v1.json` SHA256
`0362cd5a11ed31fec70d4d0f97498f887148199e3eacc9f51446cbb50f4d77cc`.
Fresh focused141-check PASS:
`out/gx-setup-local-20261111-final-v1/receipt.json`, SHA256
`0dae6de079f2ed00023bb29145a7dc83abb310154759474bc24478695348086b`.
Fresh four-step/83-host full preflight PASS:
`out/gx-setup-preflight-20261111-final-v1/receipt.json`, SHA256
`b2043dcdf7c5333af5bf1ae9acff9700f0e2148dbe0ebe42415719f8a76cf25a`.
Both source hash sets still matched after completion. Explicit original-firmware
memory-chain log SHA256
`f9dbd54c4d9e2326e560d1473afe4e88f0abb92e45d3ef0d2c6d384b99afa3ab`;
all57 actual-pool modes and older whole-chain cases passed without sanitizer
reports. Optional ZAP was not supplied. Old subset receipts below do not cover
these later source changes. No phone operations or GPU execution.

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
