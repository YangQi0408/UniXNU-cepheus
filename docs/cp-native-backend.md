# Native CP Preparation

2026-11-11: native protected CP preparation is implemented, without a startup
caller, real resource owner, CP release, ring doorbell or GPU/Metal execution.
Final replacement source acceptance completed on2026-11-11. Earlier receipts
do not accept the later output-alias correction.

## Boundary

`cepheus_gpu_cp_prepare_native` requires actual native GX performance setup,
same-session HFI boot/DCVS/START, OOB0 held and the same retained RPMh token.
Its copied external owner must protect real GX/protection/thermal state and
all CPU/device/firmware writers. Existing XNU locks and synthetic callbacks
are not hardware ownership. No production owner implementation exists.

The private singleton copies the TABLED workspace and canonical GX plan.
The actual pool acquires an exclusive six-buffer preparation pin under its
mutex: actual handles, sole CPU views, role capacities, exact SQE metadata,
digest/tail, PA/CPU identity, root geometry and all39-bit mappings are checked.
Regions/VA/bytes/table-used cannot be coherently replaced with a new PTE set
after acquisition. Ordinary view/get/put/release/publish/group sharing and
ordinary CP sealing reject pinned slots. Raw trusted CPU VAs remain writable;
this is not pmap read-only or a foreign DMA exclusion implementation.

Native CP uses the retained GX mapping, no caller-supplied register address.
Each of45 fixed protection/address-mode stores and66 vendor restore reads
has pre/post owner and halted GX/fault/power/fence checks. It rechecks all45
configured fields before capture, preserves all-ones register payloads as
data, copies the136-word snapshot to actual restore backing, and verifies
every captured word plus zero tail. A twelve-word ME_INIT packet is encoded
only into the private result; it is not placed in the ring or executed.

The final owner callback is revalidated before sealing. The actual pool
transitions the exact preparation pin directly to a sealed group under one
mutex, without an unpin-to-seal race. Final restore comparison and pool
validation precede atomic caller result publication. Any post-store failure
retains/quarantines CP and HFI backing; pre-store refusal can surrender the
preparation pin. The unexposed original firmware file snapshot is separate
from uploaded firmware/HFI backing and remains CPU-releasable.

Native result storage also rejects every actual live pool allocation, not
only CP buffers, and all known register mappings/private records. A pool
allocation in transition refuses output validation without racing its CPU
pointer. Late output failure returns failure and quarantines exposed backing;
it cannot publish a success result into HFI queues or silently return success.

Native GX SMMU now requires this private capture provenance, exact saved
publication and live restore comparison. An ordinary CPU-only publication
cannot substitute for native capture. The ordinary seal remains a CPU/pool
building block for test and internal use, not hardware provenance.

## Interim Evidence

`out/cp-build-pin-20261111-v4/` passes10 actual-pool modes with original SQE,
ASan/UBSan, coherent VA/PTE mutation, SQE metadata/tail, table/restore corruption,
blocked concurrent release, output aliases, refusal and atomic seal. Earlier
v1/v2/v3 compiler/fixture failures are retained and not accepted.

`out/cp-native-focused-fast-20261111-v1/receipt.json` passes150 focused steps,
including453 PROT_NONE native CP owner boundaries, copied inputs, recursive
refusal, halt/fault/readback failures, final owner loss, output atomicity,
native/staged AArch64 compilation and analysis. Its source hashes matched
at completion. Receipt SHA256:
`8c1fbbbff7476d423f9ea625d80e955c3b1e4a723d07bea861565b08986b399f`.
Subsequent fixture corrections and combined acceptance are not covered by
this receipt. `out/cp-native-preflight-20261111-v1/` completed four local
prerequisites and seven production analyses, before those fixture corrections.

`out/cp-native-actual-chain-20261111-v1/` passes modes0..11, then mode12
fails a fixture assertion incorrectly requiring the unexposed firmware-file
snapshot to be retained. Production CP/HFI backing is quarantined as required.
The fixture is corrected to check actual CP retention and snapshot release.
This incomplete run is not accepted as the24-mode final chain. Registers,
firmware ACKs, IOKit and external owners are simulated in all these tests.

Combined final-v1 subsequently completed153 steps PASS, source unchanged,
including all24 actual CP/GX SMMU modes, original68 HFI/native-pool and12 CP
publication modes, earlier full memory chain,10 preparation-pin modes and no
ASan/UBSan reports. Receipt SHA256:
`5abac3ca57fdaddd50f8f3f02b3c75a0a8f046401306e73cd4e831951d608110`.
Actual24-mode chain log SHA256:
`5f31e1a391ace2101a76f57a7ee43fe497bb047bc22e80c938a1c7380a3eee55`.
Original memory chain SHA256:
`d6d6cc7ed42c2dcd5f92ed1776227c2de4377fa254d2304898a7d108817afa87`.
Full preflight final-v1 passes four prerequisites/83 host programs/seven
production analyses, receipt SHA256:
`48acc81791eece4b21c88bf2096ed81861de628af626860e6ceb366bbe6e8102`.
These receipts predate the all-pool/late-output correction and do not accept
the later source. Final-v2 was terminated with exit143 after source changed;
its RUNNING receipts are incomplete, not acceptance. Fresh final-v3 runs on
frozen source and adds a25th real-pool HFI-output-alias refusal plus late
output-loss tests in both native fast fixtures. Optional ZAP is not supplied;
this does not claim the existing original ZAP file is missing.

## Accepted Source

Frozen final-v3 passes153 focused steps, including all25 actual native CP/GX
SMMU modes, the original68 HFI/native-pool and12 CP publication modes, the
earlier memory chain and10 preparation-pin modes. No ASan/UBSan reports were
found. Both final source hash sets and all153 focused log hashes were checked
independently after completion. MMIO/ACK/IOKit/external owners are simulated;
original GMU/SQE files are supplied explicitly. This is not hardware execution.

Focused receipt: `out/cp-native-focused-final-20261111-v3/receipt.json`, SHA256
`42df3226e5c903cf6bfb6b1af265cee69ba87f8a71b2f4a35421a291438bf9cd`.
Full four-step preflight/83 host programs/seven production analyses receipt:
`out/cp-native-preflight-final-20261111-v3/receipt.json`, SHA256
`e4e61c8c894231c58a8a70dfce49484e5754b7e320f1b2b56b9bef53a59190eb`.
Original memory chain log SHA256:
`2a7af399320712c1086d31beaaf63068c1820652feeea9320ad0cd629792485e`.
Actual25-mode chain log SHA256:
`61e225dbac0b276fb046d366b91e5b2828dd039d205ca4631eb8222d69a56efe`.
Actual10-mode preparation chain log SHA256:
`f4468b684d546dcfb38c48b2395ce9c0aace31153bc996211e124d731de2fd0f`.

## Full Build Failure

GX SMMU commit `4cc8d2c95d311374390913bcc029127f069840bb` was pushed and
CI42929469961 dispatched once. It failed on2026-11-11 at the toolchain host
regression, before Build XNU. Its unoptimized macOS fixture used a naturally
four-byte-aligned `ceph_smmu_install` stack output where the native contract
requires eight-byte alignment. Optimized Linux placement masked the fixture
defect. The same unoptimized Clang/UBSan failure reproduced locally with zero
owner callbacks and zero effects. Explicit `_Alignas(8)` fixes the actual
output and recursive-test output without weakening production refusal.

Failure log: `out/ci-42929469961-failed.log`, SHA256
`67cce68e7b44a3f36d6570018419cd6927b4b4068b5b4cf9944c2ea2ef48fa9a`.
The run produced no valid downloadable artifact; no kernel was accepted.
The preceding missing IOKit CP dependency failure remains documented in
`gx-smmu-native-backend.md`. A single replacement full build follows final
accepted source, not a duplicate run or a claim that local tests are linking.

## Remaining Work

Next native work is halted ring/SQE configuration bound to the installed GX
context, retained protection record and exact CP publication. Real resource
ownership/inherited request ledger, both TBUs and PAS13/secure authentication
are independent unresolved hardware boundaries. PAS13=false and
supported=false remain refusal conditions. CP remains halted throughout.
The already prepared manual-fastboot tail diagnostic is separate and has not
been booted; no phone operation is part of this batch.
