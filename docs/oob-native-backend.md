# Native GPU OOB0

2026-11-11: native acquire/release uses this boot's actual private CM3 startup
record and its retained GCC/GPUCC/GMU mappings. It is unhooked, not a resource
owner, HFI boot completion, GX initialization or CP execution.

## Admission And Lifetime

No caller-supplied HFI version or mappings are accepted. CM3 must have actually
published HFI_READY without error; its mapping/config/version identity is
rechecked before and after external ownership validation. The copied external
owner must still own the real OOB0, workspace/HFI, thermal and foreign-writer
lifetime. That production owner is not implemented here.

Actual RPMh checks surround ownership validation. Before accesses that might
lead to effects, the scope is permanently retained, including uncertainty.
Every GMU access first checks GCC/GPUCC power/access clocks. Live reset=0,
init result=0xbabeface, HFI status bit0/version identity and fault checks precede
and follow OOB accesses. No off-domain GMU read is used to establish power.
Mapping completion is not bus-fault containment or global writer exclusion.

Acquisition rejects visible pending inherited host request/release bits in
HOST2GMU_INTR_RAW_INFO and an already-set response ACK. Clear raw bits do not
exclude firmware-held inherited requests; the real owner must prove that.
Only versioned GPU OOB0 request, ACK clear and drained release are written.
Both GX status bits6/7 must be clear for HELD. No arbitrary OOB ids, interrupt
reset, remapping, GX fence changes, CM3 reset or automatic recovery exists.

Time/delay uses actual kernel time and 1us delay, accepted elapsed1us..100us,
100ms/count10000 bounds, backward/stopped/excessive-delay refusal. Exactly two
attempted stores acquire HELD; a release adds one store. Release closes CP
admission/drains through the copied owner's callback with pre/post checks.
One release admission attempt only: refusal keeps HELD and cannot retry.
Release is not proof of GX off or DMA quiescence.

Shared OOB reads/time publish output only after postvalidation. Store/barrier,
pause and drained calls have pre/post guards. Native failure is sticky;
late final validation failure changes successful output to FAILED. The acquired
transaction finishes once, and an attempted release finishes once more;
resources/RPMh/idle remain retained. Caller output mutation cannot alter the
private state used by release. Lock order remains GPLL0->RPMh->idle; owner
callbacks cannot reenter serialized APIs or release retained scopes.

## Local Evidence

`out/oob-native-local-20261111-final-v3/receipt.json` PASS, SHA256
`afdeff4a251699ca592f712d9b4373e15414fb72a431ec24eb66078db3d2b065`.
It runs actual native CM3 startup/publication followed by native OOB with
synthetic hardware/time/idle: 8863/17728 acquisition and 2698/5396 release
owner/scope PROT_NONE boundaries, copied callbacks/reentry, pending inherited
raw/ACK refusal, missing CM3/error/version/reset/HFI/fault rejection,
powered-down PROT_NONE windows, two HFI bit versions, delayed/silent ACK,
stopped/backward/excessive time, busy release and late final record failure.
Host/ASan/UBSan/native AArch64/static analysis/independent staging PASS;
source hashes still match after the final run. None is hardware execution.

Pinned vendor commit `36f369fdb77319a2984a48b89f025ac3e5f97386` is byte-compared
for adreno_a6xx_gmu.c, a6xx_reg.h and kgsl_gmu_core.h. Audit checks OOB0 id,
versioned bit formulas, fixed register offsets and100ms source timeout.

Earlier v1 predates the pending-raw check; final-v2 focused/preflight FAIL is
preserved. GDB located that crash in the test peer callback reading a deliberately
PROT_NONE GMU page, not production MMIO. The fixture suppresses fake ACK reads
when the modeled domain is off, without relaxing production guards.
Final-v3 complete preflight PASS, four prerequisites/79 host/seven production
analyses: `out/oob-native-preflight-20261111-final-v3/receipt.json`, SHA256
`2cde8885d30cb158adefe4024a36754d388abf10ed54466a918369e595800752`.
All recorded source hashes still match after completion; no source edits
occurred during either final-v3 acceptance.

All copy manifests/host lists/verifier/no-startup checks include the entries.
CI42886831693 @8c539ca contains QMP/parent binding, NOT this OOB follow-on.
Full XNU link/runtime remain separate; PAS13=false/supported=false rejection
and CP halt remain unchanged. GPU/Metal have not executed.
