# Native GX SMMU

2026-11-11: `cepheus_gpu_gx_smmu_install_native` implements the separate
SID0/mask0x401,39VA/40PA path. There is no startup caller, real resource owner,
PAS authentication or CP unhalt. GPU/Metal have not executed.

## Boundary

The one-shot singleton requires actual native GX setup, retained HFI boot/
DCVS/START and OOB0 HELD under the same RPMh token. Every mapping and access
checks the existing GX halted/fault/power/fence path and the copied external
owner. That owner must independently protect both TBUs, inherited/foreign
writers, exact CB/slot/ASID, CPU/DMA backing and thermal state; no production
implementation supplies that ownership yet. Local locks do not create it.

Only SMMU `0x2ca0000/0x10000` is mapped. Live geometry and a full stream census
reject GX-mask collisions/foreign context use. The shared selected-context
sequence is admitted by separate exact GMU and GX wrappers; legacy GMU install
and MMIO interfaces still reject GX plans. Shared reads now preserve failed
output and revalidate after their callback. No global reset, foreign/secure CB
write, FSR clear, TLBIALL, CP release or trust-control fallback is available.

The actual CP pool pin derives all six views; full SQE/PTE/restore identity is
checked before publication.15 attempted effects publish before TTBR and keep
the stream faulting until selected-context/stream enable. Scoped ASID sync is
bounded; frozen/reversed/late clocks refuse. Success ends the temporary pin,
not the boot-lifetime backing. Post-publication uncertainty quarantines CP;
final owner callback loss cannot leave success visible. Earlier HFI/GX/OOB
ownership remains retained even if the new operation fails before stores.

Publication does not prove restore MMIO provenance, CPU pmap write protection,
global writer exclusion, actual TBU readiness or secure access. Those remain
separate requirements before a production caller or GPU submission exists.

## Evidence

Initial fast ASan/UBSan test passes234 PROT_NONE revocations, fixed SID0/39VA
registers, map/null/alignment/overflow loss, final callback loss, copied-input
mutation, recursive refusal, cold/foreign/fault checks, sync/clock failures,
cleanup retention, backing-output alias and legacy GMU refusal. Its upstream
HFI/OOB/GX and pool are synthetic. This is not final-source acceptance.

Actual C/C++ pool with explicit original GMU/SQE and actual native GMU SMMU,
upload, CM3, HFI, OOB and GX setup passes all10 modes in
`out/gx-smmu-actual-pool-20261111-v4`. Registers, firmware ACKs, IOKit and owner
are simulated. v1/v2 compile failures are retained, not counted as passes.
v3 passes modes0..5 then is terminated during the deliberately expensive
full-sync timeout case; it is not a completed acceptance run. Fast unit tests
retain the full1000-poll timeout; the actual-chain case instead revokes the
lease at its first delay and verifies quarantine, without dropping guards.
Fast acceptance v1 failed GCC's misleading-indentation check in the fixture;
the fixture is corrected without reducing production checks. v2 found a
native include dependency on kernel crypto headers: CP bridge declarations
are now separated into `cepheus_cp_memory_api.h`, without pulling the layout
crypto implementation into native codegen. Fast v3 passes144 steps and source
identity, but predates the latest staged-IOKit-only compilation regression.
Fresh frozen-source acceptance has completed:
`out/gx-smmu-native-local-20261111-final-v1/receipt.json`,147 steps PASS,
SHA256 `692c2856c9b8c141074dcd737ff3d819b3fb68366ce95e1667450cd31def499c`;
`out/gx-smmu-preflight-20261111-final-v2/receipt.json`,four steps/84 host
programs/seven production analyses PASS, SHA256
`70b8ebf3b622e29a0aa22f825fbc7ea606d0432d85aaa3349d8db5cdd21cbef4`.
Both source hash sets still match after completion and all focused log hashes
match. Original-firmware memory-chain log SHA256
`4dc03d41b7d6769e8c49c480853f4fd8707bfb39511caf497456429f28c64f86`;
additional actual GX SMMU chain log SHA256
`23364c73d36d507d722e7747227aa186bfdbfcde9354812ef14aa39e8d6cfcc4`.
All10 additional cases and the earlier68HFI/12CP/whole-memory cases pass,
with no ASan/UBSan reports. Optional ZAP was intentionally not supplied.
Preflight final-v1 changed during its run and cannot accept the latest source.
No phone operations; local/codegen acceptance is not full XNU link/runtime.

The earlier full XNU CI42925513101 @2d20d14 was dispatched once. It
includes accepted HWCG/GBIF/counters/CP publication, not this later GX SMMU
batch. Never relabel that kernel as containing subsequent dirty changes.

An isolated compile of archived2d20d14 staging confirms a missing
`cepheus_cp_memory_private.h` in the IOKit directory. Local host compile had
the source directory on its search path, masking this packaging defect.
The current batch stages the CP header dependency closure into IOKit and adds
a compile regression with only that staged directory plus explicit host
platform headers. It passes. CI42925513101 failed at actual XNU compilation
with this exact missing-header error; no kernel artifact was accepted.
Failure evidence: `out/ci-42925513101-failure/cepheus-touch-build-log/build.log`
line41073. Do not relabel the earlier local pass as full-link acceptance.
The next single replacement CI must use the corrected accepted source batch.
