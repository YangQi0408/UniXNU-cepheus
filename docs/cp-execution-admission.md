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
produced. The manifest and a staging-copy regression are fixed locally; the
replacement build is not artifact acceptance until it completes.

Real owner/inherited resource ledger, both TBUs, secure PA/crypto and PAS13
remain unresolved. PAS13=false/supported=false still refuses startup. Admission
is not an unhalt authorization and cannot replace a real owner or ACK. CP halt
and GPU/Metal non-execution remain unchanged. No phone boot, reboot, flash or
system switch occurred during this work.
