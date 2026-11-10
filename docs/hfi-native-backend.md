# Native HFI Version Query

2026-11-11: the private SWEET entry `cepheus_gmu_hfi_version_native`
performs one fixed A640 firmware-version query per boot. It has no startup
caller, userspace dispatch, arbitrary command, mapping, reset or CP write.
The actual SMMU installation, firmware upload and CM3 HFI_READY records must
all match the exact six-buffer pool publication and retained configuration.
Those records are prerequisites, not global hardware resource ownership.

The copied external owner must supply real power/clock/thermal and foreign
writer exclusion. The real RPMh token is checked before and after callbacks.
The pool derives the HFI CPU/PA/capacity and configuration under its mutex;
callers cannot supply queue addresses or an ACK. Temporary pinning excludes
SMMU/upload/retire/abandon. Sealing before generic queue binding permanently
retains all six buffers, including a failed or incomplete exchange. There is
no live-queue release API. Unknown raw completion integers cannot consume pins.

The fixed doorbell writes H2G `0x1f994=1`, polls G2H `0x1f992 bit0` and clears
`0x1f991=1`. The shared queue/session validates the complete request header,
ACK status and four-word response; returned firmware major must be 2. The
request is `FW_VER` with supported version `0x20000000`, matching pinned
A640 vendor configuration. Caller-supplied versions/headers are not accepted.
Actual time and one-microsecond delays have monotonicity and iteration/deadline
checks. The 100 ms exchange budget is not a measured hardware timing promise;
individual validation/MMIO costs and scheduling latency still need measurement.

RAM accesses use pool/owner/RPMh lifetime checks before and after every word.
Individual MMIO accesses remain power-gated. Exchange boundaries additionally
verify CM3 reset/result/HFI version, GMU faults and live SMMU identity/faults.
Final owner callback is followed by revalidation; late failure clears the
firmware-version result and quarantines retained backing. `packet_words=4`
is the padded packet size, NOT a count of attempted or completed RAM stores.
`doorbells` counts attempted H2G interrupt stores, not completed GPU work.

## Local Acceptance

Focused production acceptance:
`out/hfi-native-local-20261111-final-v4/receipt.json`, SHA256
`4b7609a71b709cec22e5d276069d150a5938be12139d52c7840d71b266235c14`.
Host/ASan/UBSan, native/staged AArch64 compilation and static analysis pass.
The native fixture runs actual CM3 startup but has synthetic SMMU/upload/pool
records: 129 distributed owner and 129 scope PROT_NONE samples from
6728/13458 compound guards, NOT exhaustive native fault injection. Generic
queue access guards have their separate exhaustive cases.

Full production preflight:
`out/hfi-native-preflight-20261111-final-v3/receipt.json`, SHA256
`8120f3a87a8ed377b2a2ca28f4ad6af8d2b602857c7b96445b2f31f1b5e4fad2`.
Four prerequisites, 81 host programs and seven production analyses pass.
Both receipts' source hashes matched immediately after completion. Later
changes only add the actual native/pool integration tests and their test-script
registration; do not claim these earlier receipts ran the added tests.
Earlier interrupted/failed receipts are retained, including the missing direct
`cepheus_hfi_messages.h` include. No production gate was relaxed to pass them.

The subsequent mixed ASan/UBSan run uses actual production SMMU installation,
sealed real firmware upload, CM3 startup, HFI native query and actual C++ pool
in the SAME process. Nine cases cover immediate/delayed ACK, wrong identity,
wrong request ACK, failure after receive, final owner callback failure,
inherited raw interrupt/cold queue refusal and loss after index publication.
SMMU/upload/retire/abandon attempts during the live query are rejected. All
cases pass and six buffers remain retained as required. There are exactly
ten stage mappings and no query mappings. Hardware, ACKs, idle and IOKit are
emulated; this is NOT phone GPU execution or a production resource owner.
Log `out/hfi-native-actual-pool-20261111-v1.log`, SHA256
`bbb51ccba6db9ceed7e850dcad9d4c739475fd602ccbd98774ec8102f711fbe8`.
The complete log was checked for sanitizer reports, not just exit status.
Leak detection is disabled for intentional quarantine, not a leak-free claim.
Actual pool pin modes 0..6 and the older whole-chain 0..22 also pass in this
run. Optional ZAP is absent; no authentication is attempted.

Production bytes in the above receipts are unchanged by the integration-test
addition. Complete XNU link of this batch and preceding OOB/HFI changes is
still required; CI42886831693 @8c539ca does NOT contain them.

## Runtime Boundary

No phone operation or startup hook was added. The global owner, inherited
ARC/BCM ledger, actual TBU0/TBU1 admission, secure PA/crypto and PAS13 runtime
cause remain unresolved. PAS13=false, supported=false refusal and CP halt
remain in force. GPU scratch/fence and Metal have never executed. The existing
unbooted exit-tail diagnostic remains a separate, older candidate.
