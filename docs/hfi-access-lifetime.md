# HFI Word Access And Lifetime

The shared v1/v2 queue requires an explicit lifetime validator before and
after each payload/padding/index access and each cache/doorbell callback.
Validation or uncertain publication failures latch poison: they cannot be
retried against the same queue. This is trusted-kernel lifecycle discipline,
not protection against arbitrary privileged pointer writes.

Queue indices are read into local snapshots before either output is changed.
Receive copies at most 255 words locally, verifies the header did not change,
publishes the returned read index, then commits caller output. Failure leaves
the caller payload unchanged and count zero. UINT32_MAX is legal payload but
invalid as an index. Queue/input/output/index overlap, unaligned pointers and
overflowing ranges are refused; internal helpers cannot access arbitrary RAM.
Normal-NC ordering uses actual arm64 DSB SY, not volatile as cache coherency.

The production HFI link checks complete publication identity against both the
workspace and actual memory-pool status: group, roots, SMR, CB, slot, ASID and
all six handles. It also checks retained HFI CPU/PA/capacity, layout address
and the three ABI queue/ring pointers. External validation is followed by a
second identity check. Initial binding checks all 54 table words individually
using a local candidate; refused binding leaves caller output unchanged.
Time/cache/doorbell/pause/fault paths use pre/post validation. Lease loss is
terminal even if later callbacks claim valid. RETIRING admits only the
already-active PREPARE_SLUMBER exchange, not ordinary submission.

## Local Evidence

`out/hfi-access-local-20261111-final-v3/receipt.json` PASS, SHA256
`13379064a48a2a15c9a9a0c8dbd9a3089df648822ae60d8225ce7f49b3f8f340`.
Recorded source hashes remained unchanged through acceptance. Tests cover
24 write/20 read/8 index PROT_NONE queue boundaries, and the actual link C
implementation with 109 bind/24 read/32 write PROT_NONE boundaries. The link
test uses a synthetic pool; actual pool/link/retirement integration runs in
the separate mixed C/C++ suite. Identity mutation, output aliasing, ring
wrap/padding, UINT32_MAX payload, late failure and latched rejection pass.
Native/staged AArch64 compilation, static analyses and host ASan/UBSan pass.
Largest individual reported native frame is 2176 bytes; this is not a total
kernel call-chain stack proof.

Explicit original GMU/SQE inputs run through actual pool, native upload and
whole-chain modes 0..22 with ASan/UBSan. Log SHA256
`1866de981e2ea655fb87ae9834d8707b50b141d28a5b0b657c04e490a5d85979`.
Registers, IOKit and firmware ACKs remain emulated, not phone GPU execution.
Leak detection is disabled only for intentional quarantine retention, not
a leak-free claim. Optional ZAP is absent and authentication is not attempted.

The earlier focused final-v2 log contains invalid-enum UBSan reports despite
the process returning zero; the receipt is FAIL and preserved. The C ABI
completion receivers now admit raw integers before classification. Unknown,
negative and extreme statuses refuse without consuming the actual pin.
Mixed memory and sanitizer suites now make UBSan reports terminal.
Older SMMU/upload whole-chain logs have the same issue and are explicitly
corrected in their documents; they are not sanitizer-clean evidence.
Focused final-v1 has a wrong reference path and is not accepted.

Full final-source local preflight PASS under
`out/hfi-access-preflight-20261111-final-v2`: four prerequisites, 80 host
programs and seven production static analyses. This is not full XNU link or
runtime acceptance. Receipt SHA256
`01f9d66734f3d429744d296eaa6b6f2693c65671d1913ba57e8593d587370482`.
Recorded source hashes remained unchanged. The preceding successful kernel
`42886831693 @8c539ca`
does not contain OOB or these HFI changes.

## Remaining Binding

2026-11-11 continuation: `hfi-native-backend.md` records the implemented fixed
native query, actual pool pin/seal and real SMMU/upload/CM3 integration tests.
It supersedes the next-implementation items below, not their owner/runtime
requirements. This HFI access batch was committed/pushed as `3b0db57`.

This generic link is not the actual global resource owner. Its address and
workspace still require external serialization and lifetime ownership.
Next bind fixed native HFI MMIO/time to the retained actual CM3 record,
actual pool lifetime, RPMh scope and copied external owner. Initial native
communication must use a fixed version query, not arbitrary GPU commands or
caller-provided ACKs. No startup hook, CP unhalt, PAS13 override or inherited
queue takeover is supplied here. GPU and Metal have not executed.
