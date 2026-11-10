# Native AOP QMP Transport

## Retained Completion Follow-On

Native completion is no longer a historical consumed bit. The private cold
record requires actual attempted/acquired/finished/connected/consumed and no
uncertainty, and revalidates its real RPMh token and copied owner. Late loss
is sticky, clears consumed and marks uncertainty. Owner finish is followed by
validation before result publication. The current fixture covers 3787 owner
and 7576 scope PROT_NONE boundaries (not the older counts below). CM3/HFI
consume this record, not a caller-supplied ready flag. Final focused/full
source-bound receipts are listed in `hfi-native-backend.md`; actual AOP ACK,
global ownership and before-power ordering remain unproved.

2026-11-11: `cepheus_gpu_qmp_acd_native` binds the existing cold QMP handshake
and fixed GPU ACD=1 message to actual SWEET mappings, barriers, kernel time and
delay. It is a private unhooked entry, not a mailbox owner or GMU startup.

## Admission And Effects

The one-shot adapter copies its external owner, takes the existing GPLL0
serialization lock, and requires the real RPMh/idle scope. The owner's begin
must acquire actual config bandwidth, mailbox/IPCC lifetime and exclusion of
every local/foreign writer, with no writes on failure. No production owner
implementation is supplied. A connected descriptor is not that ownership.

Before mapping or possible effects the adapter permanently retains its
RPMh/idle scope. Validation surrounds each fixed map, each individual shared
word load/store and IRQ store, and each real time/delay operation. Failure is
sticky; null mapping still performs its post-map check. GPLL0->RPMh->idle lock
order is unchanged; owner callbacks must not recurse into any of those APIs.

Fixed mappings are MSGram `0xc300000/4096` and IPCC `0x17c0000c/4`, descriptor
offset zero and notification value one. Polling uses actual kernel nanoseconds,
500us delay with accepted elapsed range 500us..10ms, iteration limits and
existing 5s connection/1s consumption limits. Stopped/backward time, excessive
pause, invalid alignment/VA overflow and invalid bus values are rejected.
CPU translation or mapper completion is not a bus-fault/deadline guarantee.

Only MAIL v1, remote link UP with both channels/local states/ACKs DOWN and
empty TX/RX is accepted. The handshake writes exactly the existing four
host-owned fields and requires remote ACK progress. Already connected or
partially inherited channels are never reset, silently adopted or taken over.
The ACD payload is the existing fixed 32-byte text, then length, readback and
IRQ notification. Only length-clearing consumption is accepted; it is not an
application-level ACD ACK. No arbitrary message, remote RX consumption or
automatic disconnect exists.

The owner finish runs exactly once after acquisition. Every keep attempt,
new connection or uncertain effect keeps mailbox/resources and the permanently
retained RPMh/idle scope. MMIO and notification counts report attempted
effects. An error never clears the channel or releases a retained mapping.
Late final lease failure clears consumed and reports uncertainty before
publishing the result. Context/lock/second-attempt refusal leaves output intact.

## Local Evidence

`test_qmp_native.c` exercises the actual private adapter and RPMh implementation
with explicitly synthetic idle and peer/time/mapping callbacks: 3786 owner
and 7574 scope PROT_NONE failure points, fixed mapping arguments, two map
failures, copied callback mutation/reentry, invalid context/token, all seven
inherited-state refusals, connected/RX/timeout/layout refusal, stopped/backward
time and excessive delay, and descriptor corruption after all 13 stores.
Clean completion is 13 mailbox stores and four notifications; tests do not
constitute an actual AOP/GMU execution.

The existing QMP shared adapter now wraps every individual descriptor/TX/RX
and payload load with pre/post ownership checks. Descriptor snapshot output
is atomic on failure. Stores/time/barriers have post checks, and mapped VA
overflow is rejected. Old QMP message/connection tests still pass.

The final `test-gpll0.py --android` run independently byte-compares and records
the pinned vendor `msm_qmp.c`, `kgsl_gmu.c` and `sm8150.dtsi`, in addition to
the existing clock reference. It checks MAIL/version, 5s/1s source bounds,
payload-before-length, IRQ mask, fixed ACD text and fixed DTS maps/offset.
Reference commit is `36f369fdb77319a2984a48b89f025ac3e5f97386`, not live FDT.

Initial preflight final-v1 failed the isolated SCM codegen fixture because
the new private header did not include its public types. The explicit public
include fixes that dependency, with a fresh successful focused SCM codegen.
Earlier focused/preflight v1/v2 are not final acceptance for the later 500us
poll correction. Fresh final-v3 focused acceptance PASS:
`out/qmp-native-local-20261111-final-v3/receipt.json`, SHA256
`5bfe6809edb1742634bb60e20b89590f53ec31fff46b576e3890277a4d4d6047`.
Full local preflight final-v3 PASS, four prerequisites/78 host/seven production
analyses: `out/qmp-native-preflight-20261111-final-v3/receipt.json`, SHA256
`fa930323fc877ec9a999cc79207d8fcb86a80d0999f0c4fc728ad4dafe6d1086`.
All recorded source hashes still match after the completed run. No source
changed during either final-v3 acceptance. Synthetic peer progress remains
explicitly separate from AOP hardware evidence.

Both local/CI copy manifests, memory staging, no-startup-call checks, host
and sanitizer lists and compiled-kernel verifier include the new entry.
Complete XNU link and hardware acceptance remain separate. CI42881254237
@d5bf1dd does NOT contain native QMP or the later native parent binding.
PAS13=false/supported=false and CP halt are unchanged; GPU/Metal have not run.
