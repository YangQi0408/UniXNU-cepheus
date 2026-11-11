# Native Halted Ring

2026-11-11: cold native ring/SQE configuration is implemented without a
production startup caller. CP remains halted. No WPTR, PAS authentication,
trust-control change, CP_ME_INIT submission or GPU/Metal execution occurs.
Final local acceptance completed on frozen source. Full XNU link acceptance
of this subsequent ring batch is still separate.

## Actual Prerequisites

`cepheus_gpu_cp_ring_native` takes a copied genuine external owner only, not
caller MMIO or addresses. It requires the retained actual native GX SMMU
singleton to be enabled, finished, unpinned and free of uncertainty, with
the original15 installation effects and the same RPMh token. Actual native
CP capture provenance and actual installed pool layout remain mandatory.
The external owner must cover GX, both TBUs, context and CPU/device writers;
local XNU locks or simulated owners cannot establish these hardware facts.

All seven fixed addresses/values are derived from the actual CP publication.
Every GX load and store has pre/post ring-owner and upstream GX SMMU/CP/HFI
checks. The14 named context fields, including stream, ASID/TTBR, permissions,
FSR and TLB status, are compared before/after each ring store and at final
publication. Every context load also has pre/post ring-owner checks. These
reads never clear faults, issue TLBI or alter the installed SMMU.

All45 retained protection/address-mode fields are checked before/after stores
and after the final owner callback. Cold base/RPTR/SQE registers and RPTR/WPTR
must be zero. An inherited halted ring is refused, not cleared. All seven
stores are read back individually and as a complete set before publication.
The final owner callback is checked again and all mappings stay retained.

The actual pool implements installed-state validation and ring publication
under its mutex. Publication checks exact PTE/SQE/restore identity and every
byte of ring/RPTR backing, including16KiB host-rounded RPTR capacity, is zero.
It only performs a memory barrier, not an MMIO doorbell or a cache-policy
change. Raw trusted CPU VAs remain writable, so foreign writer exclusion
still belongs to the genuine owner.

Caller output is written only on success, after known MMIO/private/owner and
all-live-pool aliases are refused again. Acquired failure quarantines CP/HFI
even before the first ring store because the SMMU already exposes this group.
Early refusal does not roll back the previously installed context.

## Local Evidence

The initial fast fixture passed710 owner boundaries. Additional per-context
owner checks then passed10650 boundaries. That version repeated full context
checks for every protection read; the actual-chain v1 was terminated with
exit143 and is not accepted. The revised source retains pre/post ownership
for every access and full context checks for each store and final publication,
with906 PROT_NONE boundaries passing. Earlier receipts do not accept later
source edits. This is simulated upstream/pool evidence, not hardware execution.

The actual CP pool12-mode regression passes, including installed-only
publication, full ring/RPTR-tail corruption refusal and concurrent retention.
Original SQE is supplied explicitly. Seven new mixed C/C++ modes25..31 cover
normal ring setup, inherited base, post-store owner loss, final callback loss,
HFI output alias, live TTBR replacement and host-rounded RPTR tail mutation.
These use actual GMU SMMU/upload/CM3/HFI/OOB/GX/CP/GX SMMU code and original
GMU/SQE, with emulated MMIO/ACK/IOKit/owners. Final-v2 passes all seven cases
and no ASan/UBSan reports. The final155-step focused receipt and four-step
83-host/seven-analysis preflight both pass. Both source hash sets and all155
focused log hashes were independently checked after the actual chain ended.
Vendor restore/ring registers, ordinals and role permissions also pass the
existing independent pinned-source checks. The first permissions command
used a driver directory instead of the required kernel root and failed before
testing; the corrected invocation passes, without changing the test.

Focused: `out/cp-ring-native-focused-final-20261111-v1/receipt.json`, SHA256
`55b8703f4d3d685f2bcabf83cfb20a48e7e121e18dd40b9c618c52dac0e900d0`.
Preflight: `out/cp-ring-native-preflight-final-20261111-v1/receipt.json`, SHA256
`096e87b7bfe880c95a709b7dd4dbeb2f7221b7e860a7f4f09233d30d8386e2a5`.
Actual chain logs in `out/cp-ring-native-pool-20261111-v2/`:

| Mode | SHA256 |
|---|---|
|25|`be8d90a7e1c0d7909649947931e483f9d2df0a0b5ea48911b9a02020d65374a4`|
|26|`9ec635c179598ae2b52efa84240dfcbcb5d62f735068cf03b02c3cf0aadcc0ae`|
|27|`e480fe0311b89e69a5cf86be0ef7c7395cd7e2352c0823c39b0784d36c0bb536`|
|28|`c92c835af14ef9ec75d557b7fccb642ab2b175e940239f05cd60f1c10bfdc5ff`|
|29|`7a37a8611e481d1594d78419d9671dd85a4fe89f0908fa2cf79ee8515b557b59`|
|30|`4f69356e1fe6b4b8c900bc456e61907cbe34cbf9364d2d6a02478e56b53a4389`|
|31|`60edd6c3cb7e5ffe77c4a990b6d006b191d02ae32d20c96ecfdbd44f804e6925`|

This acceptance deliberately runs only the seven new ring-chain modes, not a
fresh rerun of all earlier25 CP/SMMU cases. Existing CP pool12 modes and the
full host preflight cover the changed installed/publication pool APIs. Neither
this receipt nor any simulated scenario proves a genuine hardware owner.

## Build Identity

Previous native CP batch is committed/pushed as
`78d34a858e2a692cd5aaf3c1cdf324576e32856d`. Its single complete-kernel build
is CI42939546189. Committed source was archived locally to
`out/ci-source-78d34a8/` for independent artifact acceptance while this later
ring source changes. That kernel, if successful, cannot accept these changes.

## Next Boundary

Bind a future CP-release/ME_INIT path to actual ring, CP snapshot, installed
context and authenticated ZAP records. Real resource ownership/inherited
request ledger, both TBUs, secure PA/crypto and PAS13 remain unresolved.
PAS13=false/supported=false still refuse startup. Never use authentication
as a probe or substitute trust-control fallback for an authenticated state.
No phone boot/reboot/flash is authorized by this source work.
