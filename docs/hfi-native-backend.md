# Native HFI Session

CI42907357305 @31215d3 is now successful and independently accepted against
archived source, including linked RPMh/idle. Exact kernel/receipt hashes and
scope exclusions are in `gx-native-backend.md`. It does NOT cover later
`ceaa736` pre-cold/config changes or native GX. Earlier "building" note below
records the state at that follow-on's acceptance.

## 2026-11-11 Config And Pre-Cold ACD Follow-On

The continuation above was committed/pushed as `31215d3`. Its single full
XNU CI `42907357305` is building; it does not cover this later batch.
Native CM3 now requires the exact retained native GPU configuration. Bin1
requires a successfully consumed native QMP ACD record with the same RPMh
token BEFORE the first CM3 attempt/map/effect. Refusal leaves output intact
and permits completing the missing prerequisite. This proves ordering before
CM3, not before the earlier power stage as in the pinned vendor sequence.
QMP completion and CM3 readiness revalidate after their final owner callbacks.
The QMP token/config are checked throughout CM3 and subsequent HFI leases;
late CM3 owner/config loss quarantines its transaction. HFI cannot retroactively
accept ACD after CM3. No startup caller or real resource owner is supplied.

Fresh focused acceptance, 135 checks PASS:
`out/hfi-config-cold-local-20261111-final-v2/receipt.json`, SHA256
`262ca43202c50364570bb4a7279eb320561b445341ac5b4d64d99995bdafed29`.
Fresh full preflight, four steps/82 host programs PASS:
`out/hfi-config-cold-preflight-20261111-final-v2/receipt.json`, SHA256
`544b656ba1a9e5103a631f726904faf993f92d5d33fb6500be81933a86c262b4`.
Both source hash sets match after completion. The original-firmware actual
C/C++ pool chain now passes modes0..30, including full bin1 START, missing
ACD/retry, QMP owner/token loss, CM3 final config loss and copied-config mismatch.
Whole log SHA256
`180839b7d3e3a1f660f557bcd7c6279cfb2d1dd7a6187f1d96fd3a9c729c7543`.
No sanitizer reports. Hardware, firmware ACKs, global owner, idle and IOKit are
emulated. Modes27..30 stop before HFI sealing: CM3 quarantine is not itself
whole-pool quarantine; the fixture's later retirement attempt quarantines it.
These receipts do not cover any subsequent GX backend edits or runtime success.

## 2026-11-11 Accepted Continuation

The next batch adds native configuration from the actual retained RPMh DB and
completed boot-local fuse sample, then continues the same sealed sequence-1
query session. Fixed PERF(59), BW(160), ECP-off and CORE_FW_START(2) commands
are sent; bin1 additionally sends ACD(17), enables ACD and LM throttling.
Separate fixed DCVS(level1, BW INVALID) and START entries require the preceding
ACKs, and START checks the actual native OOB0 HELD record and access path.
No rebind, queue-index reset, remap, arbitrary command, GX access or CP unhalt
was added. The session now rejects a deadline crossed inside notify/read/fault
callbacks before publishing a reply. Every admitted failure retains/quarantines
the actual pool; success after a final owner callback is revalidated.

Focused final-source acceptance:
`out/hfi-start-native-local-20261111-final-v2/receipt.json`, SHA256
`346a6447538ed5d28be47caacb3b386d4b34c49ccea5c3cd0763c1fe045d7fcf`.
135 checks pass, including host/sanitizers, native/staged compilation and
static analysis. Native configuration tests cover 1925 owner PROT_NONE points,
two parses, typed equality, fuse/DB/scope loss and late callback revocation.
Full preflight `out/hfi-start-native-preflight-20261111-final-v2/receipt.json`,
SHA256 `3228c6193ff590415863862e4b55b410cfefa962fc203cccdf92f9705c5a8e1f`,
passes four prerequisites and 82 host programs. Both source hash sets matched
after completion. Earlier failed/intermediate receipts are retained.

The explicit original-firmware mixed C/C++ actual-pool chain passes 24 modes
(0..23): query, bins0/1 boot, each bin0 boot ACK failure, late callback/config/
CM3 failure, bin0 DCVS/OOB/START and its failures. Complete log SHA256
`c0601d764aaf927e573177bb1a5e4f886651e1260b05a0f488a90ad09f90a602` at
`out/hfi-start-native-local-20261111-final-v2/real-firmware-memory-chain.log`.
The whole log has no sanitizer report; intentional quarantine leak detection
is off. Firmware peer, MMIO, global owner, idle and IOKit are emulated. Full
bin1 START and production proof of AOP ACD consumption BEFORE CM3 release
are not covered by this batch: its bin1 fixture sends QMP after the query.
That ordering must be fixed before a production startup caller is admitted.

The configuration owner must verify the REAL board/profile against
`CE_GPU_CONFIG_FDT_SHA256` and freeze the actual DB. No real global owner or
recorded live FDT is supplied by these tests; matching parsed tables do not
grant resources. HFI callbacks execute under GPLL0 serialization and config
checks acquire `rh_lock`; callbacks must not recurse or take the reverse order.

## Accepted Prior Kernel

CI `42900020339 @471e2237eae662a1549655dc4b2a7fcc92703b3c` succeeded.
Archived-source independent acceptance passes 101 core/35 memory symbols,
no unresolved symbols, touch/PA64/native CPU+SCM/tail checks. Kernel is
17492488 bytes, SHA256
`98b66afd4039e0fc64f208afbb8a88a6ba1a51fc2f9bf76a90a102424b55f545`.
Receipt `out/kernel-verification-ci-42900020339.json`, SHA256
`079cbecba790c457c64b0446c832fa8acb7180ba1e95af3f5a612d4cb7e64365`.
Actual linked RPMh/idle also pass. This kernel includes OOB/query/pin work,
NOT the new configuration/boot/DCVS/START batch above. No image was packaged
or booted. Earlier pending-link notes below are historical for that prior batch.

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

Production bytes in those earlier receipts were unchanged by the integration
test addition. That batch's complete link is now accepted at `471e223` above;
CI42886831693 @8c539ca does NOT contain OOB/HFI/pin changes.

## Runtime Boundary

No phone operation or startup hook was added. The global owner, inherited
ARC/BCM ledger, actual TBU0/TBU1 admission, secure PA/crypto and PAS13 runtime
cause remain unresolved. PAS13=false, supported=false refusal and CP halt
remain in force. GPU scratch/fence and Metal have never executed. The existing
unbooted exit-tail diagnostic remains a separate, older candidate.
