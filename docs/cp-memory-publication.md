# CP Memory Publication

2026-11-11 continuation: a separate GX CP six-buffer seal/pin is implemented.
It does not install GX translation, authenticate ZAP, release CP or execute a
command. There is no startup caller or real resource-owner implementation.

## Actual Pool Boundary

`cepheus_cp_workspace_seal` accepts only the CPU TABLED workspace after its
protected restore snapshot. The actual pool checks sole views, live handles,
supplied-view identity, distinct backing, fixed role capacities, exact SQE
payload digest/tail, all39-bit PTEs, restore-list offsets/protection/address
modes/digest/tail, and canonical SID0/mask0x401 context encoding. The seal
copies regions/context/handles into pool-owned records and orders Normal-NC
backing before an all-or-none transition to SHARED. It rejects any existing
pool group's overlapping CB, slot, ASID or GX stream; this is not a foreign
hardware stream census. Later GMU sharing rejects conflicts with CP groups.

CPU get/put/release, ordinary workspace release and GMU install/upload/HFI/
retirement/abandon interfaces reject a CP group. The sole trusted CPU VAs stay
mapped; the seal is lifetime discipline, NOT pmap read-only protection or
exclusion of privileged/DMA/secure/firmware writers. Live FDT, geometry/TBU,
reserved VA/GMEM/global ownership and security still require real evidence.
Restore structure/digest checks do not establish where its values came from;
the future native capture must bind actual guarded MMIO and retained records.

`ceph_memory_cp_acquire` derives the actual views under the pool mutex and
pins once. `publish` revalidates all backing and issues a real barrier before
marking exposure. REFUSED is legal only for an unexposed pin and never frees
sealed backing. INSTALLED is legal only after exposure and retains for boot;
it describes the installer's result, not a caller-ready flag for submission.
Uncertainty quarantines all six CP buffers and makes the allocation pool
terminal. No GX stop/unmap/TLB-backed retirement is implemented yet. Invalid
C ABI result integers are rejected before classification. Outputs remain
unchanged on refusal, and misaligned/overlapping output storage is rejected.

## Evidence

Actual C/C++ pool tests with explicit original SQE pass12 modes0..11 under
ASan/UBSan, including GMU coexistence, conflicting CB, duplicate handles,
corrupt tables/SQE/restore/tail, identity mutation, duplicate pin, exposure
refusal, integer extremes, uncertainty retention, output alias/alignment and
1000 concurrent release/retire refusals. IOKit, PA and MMIO are test doubles.
`out/cp-memory-pool-20261111-v6/` is focused host evidence, not a kernel link
or hardware run. Deliberate quarantine leaks are not checked.

v1 failed fixture compilation; v2 passed before output guards. v3 correctly
found a production bug: acquire checked a temporary output rather than the
caller's final buffer and admitted a backing alias. The fixed acquire checks
the actual output under the pool mutex; v4/v5/v6 passed progressively stronger
cases. All old logs remain. Frozen-source final-v2 acceptance has completed:
`out/gx-perf-cp-local-20261111-final-v2/receipt.json` PASS,141 steps, SHA256
`04c809ab03dcfac89144b2a0a22bcd6d91b19f26010245cacf1484bbed92b4b8`;
`out/gx-perf-cp-preflight-20261111-final-v2/receipt.json` PASS, all four
prerequisites/83 host programs, SHA256
`92b7c19cd8b691b9da78289a5b6e754d4e7c3052af16eba5b9f7722dbcbc7c84`.
Both source hash sets still match after completion; all focused log hashes
match. The explicit original GMU/SQE memory-chain log SHA256 is
`7ff983a4c91ee924e61783b0c7422a2f0777615b0e7400d88bb4f2cd36f88361`.
The chain passes68 HFI/native-pool modes and12 CP-pool modes plus the earlier
memory tests, with no ASan/UBSan reports. Optional ZAP was intentionally not
supplied. v1 runs changed while executing and cannot accept latest source.
This is host/codegen acceptance, not a full XNU link. No phone operations or
GPU/Metal execution.
