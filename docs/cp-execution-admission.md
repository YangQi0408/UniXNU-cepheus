# CP Execution Admission Boundary

2026-11-11: a private CP execution admission boundary is implemented without a
production caller. It binds the already-retained native ring, CP protected
snapshot/publication, enabled GX SMMU context and authenticated ZAP allocator
epoch under the SCM/GPLL0 lock order. It does not clear CP halt, write WPTR,
submit a packet, install a new context, perform PAS work or execute GPU code.

## Accepted Identity

The pure plan requires exactly seven configured ring stores derived from the
actual CP publication addresses. The copied CP publication, GX SMMU plan,
ring plan and ZAP claim are compared field by field; padding and `memcmp` are
not identity. The six pool handles must be nonzero and distinct, all five CP
regions must have canonical bounds and role permissions, and caller output
cannot overlap any copied input.

The native wrapper requires a live `CE_ZP_LIVE` epoch and the retained
authenticated allocator record. It also requires the actual ring configured
with seven writes, actual CP sealed with45 writes/66 reads, and actual GX SMMU
enabled with15 effects and no pin/uncertainty/failure. The exact RPMh token
must still match. Before and after the external owner validation, all three
native live checks run again.

The external owner must now provide both the accepted owner-evidence gate and
the exact snapshot that gate retained. A self-sealed record, a refused gate, or
mismatched owner/lease/resource epoch, ACK or RPMh ledger identity is rejected
before an admission attempt. The gate and snapshot are copied, compared again
after taking the locks, and the admitted record carries owner/lease/resource
epoch for every later check. This binds record identity only; it does not turn
a synthetic snapshot into a real TBU/RPMh owner or hardware ACK.

Lock order is `ceph_scm_info_lock -> ceph_gpll0_lock -> pool lock`. The
authenticated allocator query has an explicitly unlocked variant for this
serialized path, avoiding a reverse pool-lock acquisition. The external owner
must be a genuine GX/TBU/CP and CPU/device writer lease. A simulated owner or
local mutex is not proof of hardware ownership.

Admission is one attempt. A failed attempt is sticky, and any later identity,
live GX/CP/ring or authenticated-ZAP failure during a check also permanently
invalidates the admission. A wrong admission token alone does not consume the
valid admission. External owner resources are retained on every attempted
path, including lock-busy and validation failure. Output is written only after
the final live validation; refusal leaves caller output unchanged.

The owner `finish` callback receives the sender-owned admission record read-only
while SCM/GPLL0 serialization is still held. On a successful admission this is
the exact seven-write ring, CP publication, GX SMMU plan, ZAP epoch/claim and
RPMh token identity that was admitted. On a refusal the record may remain
empty; `keep=1` still means the external owner resources are retained. The
callback must verify the snapshot against its own ledger rather than treating a
nonzero pointer as ownership proof.

The focused binding receipt is
`/tmp/owner-admission-binding-local-v2/receipt.json`, SHA256
`57c8f49f616d58e4c0b84364c5bbba5c8af5767c280300b5708203dbfddb8917`.
It passes host, ASan/UBSan, actual SWEET AArch64 assembly, staged AArch64
compilation and static analysis, with no phone operation or GPU execution.
The full offline preflight
`/tmp/gpu-offline-owner-admission-20261111-v1/receipt.json` is
`PASS_WITH_SKIPS` with26 PASS,0 FAIL and6 optional-input skips; its SHA256 is
`d24b73590b5bf2de6632c10951ba31533993d7e92551ce49d9dde4f63956ae7b`.

The final focused receipt is
`/tmp/cp-admission-finish-local-v2/receipt.json`, SHA256
`3a056ad4359c18a4b96c832ce3157773dda01e12bc05f1eb8e4873b8b4cc2791`.
It binds 130 source files at `c190a09`; host, ASan/UBSan, actual AArch64
codegen, staged codegen, real-firmware chains and static analysis all passed,
with `phone_operations=false` and no GPU execution. The full preflight is
`/tmp/gpll0-admission-finish-preflight-v1/receipt.json`, SHA256
`0beab8945233b91ccadb7959e49ee833639a71ab4a32a5700413bb9324223ce9`;
all four steps passed and its 382 `xnu/`/`scripts/` source hashes still match.
The ARC/BCM final focused receipt is
`/tmp/active-lease-local-v4/receipt.json`, SHA256
`f8cf24cf5ef8604305f6ab03353b0f9c232e6cc550fa7e59ed384a1b74d468ea`.

The first complete CI for `c190a09` was CI42971631606 and failed in the macOS
host job because `test_arc_active.c` declared the lease snapshot before its
private full type definition. The declaration was moved after the private
header include; the final focused ARC/BCM receipt above covers the repaired
test source. That failed run produced no accepted kernel artifact and is not
presented as evidence for this batch.

The owner-finish snapshot batch was committed/pushed as
`aa1947b893d5d36355fde1aa3115e22aa497eb8e`. CI42974023175 completed SUCCESS
in 35m23s and downloaded artifact `cepheus-touch-kernel` from the same run.
`device-commit.txt` exactly matches that commit and `SHA256SUMS` verifies.
The kernel is17564192 bytes with SHA256
`9ddd8ffc89728b396a2e46fae2d2e282494c2a06dccf6bd6502ef86eaab15f7f`;
the build log SHA256 is
`ed89ed573a8dcbc21a12e8e55f4d974f06fba9d4dfcb6eb11a4061134bdba5a7`.
`llvm-nm -u` is empty. Independent verification from the exact commit archive
stages the source and passes `verify-gpu-kernel.py`:115 core entrypoints,
49 memory/firmware APIs, zero unresolved. Both CP admission symbols and the
authenticated ZAP unlocked query are linked, as are the ARC/BCM raise paths,
ring and submit. This accepts `aa1947b`, including the fixed-slot ACTIVE lease
and owner-finish admission snapshot, as a linked kernel build only.

Acceptance reproduced several verifier invocation mistakes locally before the
correct archived-tree invocation passed; those transient local errors changed
no source and are not counted as build failures. The final command used the
archive's own `0087` script from an empty `bsd/dev`, then explicitly added the
CI-owned ARC/BCM active and RPMh scope headers before verification.

## Local Evidence

The identity test covers exact ring/CP/SMMU/ZAP epoch matching, handle and
context mismatch, layout/alias/bounds/permission refusal, invalid tokens and
output overlap. The native wrapper test covers lock order, one-attempt
semantics, busy-lock retention, output aliases, wrong-token non-stickiness and
sticky ring-identity, live-GX and authenticated-ZAP loss. Both run under host
and ASan/UBSan and pass static analysis.

Focused/full local preflight completed on the accepted production source:

- Receipt: `/tmp/gpll0-admission-final-v2/receipt.json`
- Receipt SHA256: `17e29e90417fb50bdddf6751937838effbe83dbe977de9e1cd8d7ce79aecd60d`
- Four prerequisites, host and sanitized runs, actual SWEET AArch64 codegen,
  staged codegen and seven production analyses all PASS.
- Real original GMU/SQE GX SMMU modes0..24 and ring modes25..31 PASS under
  ASan/UBSan, with emulated MMIO/ACK/IOKit and synthetic hardware owners.

The real-firmware logs are preserved under:

- `/tmp/gx-smmu-admission-base.8YrhgT/out`
- `/tmp/cp-ring-admission-base.C6bAVt/out`

These prove firmware parsing and the actual C/C++ backend chain, not phone GPU
execution. No sanitizer reports were found.

The receipt names parent commit `a538e53` because the final test-fixture
cleanup and documentation binding followed it. Those edits did not change
production bytes; the receipt is not presented as a fresh `f944723` run.

## Previous Build Identity Correction

CI42959844615 completed SUCCESS for `a538e534801b69765066fef4b801afcf3cc77a3b`.
The downloaded `kernel.macho` is17563504 bytes with SHA256
`0758ee753fa10f0c366a9b29bac339816efdf7f6ba884a89367533ee7f5df229`.
This corrects the earlier handoff text that attached17546448 bytes and
`358faebd...` from a different run. The kernel links `_cepheus_gpu_cp_ring_native`,
`_cepheus_gpu_cp_prepare_native`, ZAP load/stop/status and authenticated ZAP,
but not this subsequent admission batch. It does not accept current HEAD.

## Remaining Boundary

This batch is committed/pushed as
`f9447238ca206537a6e263da15694cd8d663a975`. Its single complete-kernel build
CI42964201439 passed steps1..8 but failed during `0087` staging because the
workflow copy manifest omitted both admission headers. No kernel artifact was
produced. Commit `db33eff11c8acf11b00008e32e36e1d87a0bf8a1` fixed the copy
manifest and added the staged-source regression. Replacement CI42967566527
completed SUCCESS and its artifact passed independent verification: kernel
17580568 bytes, SHA256
`ee325d473d1a2c7418f0b722b32d67ef646d89b44571a78eddb73d5387cc8235`;
archived-source staging plus `verify-gpu-kernel.py` reports 115 core entrypoints,
49 memory/firmware APIs and zero unresolved symbols. Both CP admission symbols
are linked. This accepts `db33eff`, not the later fixed-slot ACTIVE lease batch
at `c190a09`.

Real owner/inherited resource ledger, both TBUs, secure PA/crypto and PAS13
remain unresolved. PAS13=false/supported=false still refuses startup. Admission
is not an unhalt authorization and cannot replace a real owner or ACK. CP halt
and GPU/Metal non-execution remain unchanged. No phone boot, reboot, flash or
system switch occurred during this work.
