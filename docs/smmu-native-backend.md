# Native GMU SMMU

2026-11-11: `cepheus_gpu_smmu_install_native` binds the existing scoped
SMMUv2 installer to actual SWEET mappings and the actual six-buffer C++ pool.
It has no startup caller and does not grant hardware ownership or start a GPU.

## Contract

The boot-lifetime singleton copies the owner, canonical SID5 plan and pool
publication. It shares the GPLL0/counter/power/CM3/PDC lock. Owner callbacks
must be non-sleeping and non-recursive, acquire genuine SMMU/TBU/GMU access,
reserve the selected stream/context/ASID and exclude relevant writers and
resource-affecting idle transitions. No production owner implements this yet.
Software locking and a pool pin are not hardware ownership or bus-fault
containment. Owner acquisition cannot hide device stores or firmware upload.

Fixed mapping requests are GCC `0x100000/0x90000`, GPUCC
`0x2c90000/0x9000`, GMU `0x2c6a000/0x30000` and SMMU
`0x2ca0000/0x10000`. Every map, including a null return, has pre/post owner
validation. GPUCC and GMU mapping footprints overlap on 16 KiB hosts, as
documented for CM3/PDC; owners must protect both aliases. Actual GMU access
prerequisites, reset=1 and active MEMNOC/DVM branches gate every SMMU access.
The SMMU/TBU powered path still needs the real owner's independent evidence.

Live ID0/1/2 decoding checks geometry, then all stream groups and the selected
SCTLR are inspected for collisions and foreign context use. Only canonical
SID5 installation is accepted. There is no global reset, secure/foreign write,
TLBIALL or detach. The existing 15-effect sequence includes table publication
and ends with selected-context/stream enables. Native delay uses kernel time
and `delay(1)`, rejecting frozen/reversed clocks and pauses outside 1..100 us.

The pool pin verifies exact group/handles/root and sole SHARED views, validates
materialized PTEs and fresh HFI, and derives GMU configuration from actual
backing rather than caller pointers. Retirement and abandonment are refused
while installation holds the pin. Table publication validates the actual
Normal-NC backing and orders it. CPU/device/foreign writer exclusion remains
the owner's job. Installed completion ends the temporary pin, not the retained
hardware lease. Any post-publication uncertainty quarantines the group and
terminates the pool without freeing it. Failed cleanup retains the pin and
reports QUARANTINED, including acquisition failure before device stores.

Shared SMMU MMIO now rejects pointer overflow, checks before/after 32/64-bit
reads and stores, preserves failed-read output and checks post-access ownership
before rejecting invalid bus values. Native access failures are latched and
terminal; successful completion revalidates before releasing the temporary pin.

## Local Evidence

Accepted focused receipt:
`out/smmu-native-local-20261111-final-v2/receipt.json`, PASS, SHA256
`c4a53b18bed72b580e36530aa392fb2c28fa5ac1c38e6ca07b1b8c2c9fe80477`.
It covers 8122 individually revoked PROT_NONE boundaries, map failures and
coincident revocation, copied-input mutation, recursive rejection, geometry/
stream/reset/off-clock refusals, sync completion/timeout and clock failures,
pool cleanup errors, sanitizers, native/staged AArch64 and static analyses.

Accepted full local preflight:
`out/smmu-native-preflight-20261111-final-v2/receipt.json`, PASS, SHA256
`3047e55c5aa4bad094ba4a455d2fa67b94efd358674526ad43a5a6b1788c9b00`.
All four prerequisites pass, including 74 host programs, seven production
analyses, staging and no-startup checks. These are not full XNU link acceptance.

Explicit real GMU/SQE-input ASan/UBSan run exits 0:
`out/smmu-native-local-20261111-final-v2/gmu-chain-asan.log`, SHA256
`5b433646641f516f5c374f36e5625f5ffaf0f778d85b29259575cbb272054cf8`.
Actual pool pin modes 0..7 (including concurrency), native-plus-actual-pool
modes 0..5 and whole-chain modes 0..22 pass with emulated registers/IOKit.
Leak detection is disabled for deliberate quarantine retention, not a leak-free
claim. Optional ZAP inputs were not supplied; default preflight skips
proprietary inputs without explicit paths. No phone GPU execution was observed.

Receipts bind dirty sources at parent `e458ee7`, notably memory.cpp SHA256
`ab97aed3c857bd797208cded732bbca4d626c184a961e6b3caa7aa80626fede4`
and native-private SHA256
`139bbbfb6c9c08ab3638fcf45d19cbf18247b9d7fe4d9bc8e22eae9516c3747a`.
Earlier final-v1 predates the cleanup-failure result correction and remains
historical evidence only.

Read-only Android commit `36f369fdb77319a2984a48b89f025ac3e5f97386`,
`arch/arm64/boot/dts/qcom/msm-arm-smmu-sm8150.dtsi` SHA256
`8012448d8a696401239492de414a54b637041889c31116650e8a3f0b874413d8`,
corroborates the base, skip-init, clocks and two TBUs. Captured-FDT identity
remains `bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274`.
Source inspection does not prove actual TBU accessibility or ownership.

## Next Integration

Continue native firmware upload/bindings and real inherited-resource owner/
idle coordination, then integrated startup. The combined native batches still
need complete XNU compile/link and independent artifact acceptance. No separate
CI, image or phone operation was performed for this unhooked module.

Accepted unbooted exit-tail diagnostic stays CI `42835691416 @2bf48ab`, boot
SHA256 `b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`.
It excludes later native work and is not relabeled HEAD. PAS13=false,
supported=false, secure PA/crypto and CP halt remain independent gates.
GPU scratch/fence and Metal have not executed; the overall goal stays active.
