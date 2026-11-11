# BCM ACTIVE Native Transport

2026-11-10: `cepheus_gpu_bcm_raise()` now uses the actual private
`sweet_rpmh.c` ACTIVE transport. No startup hook, firmware call, phone command,
boot package or GPU execution was added. Real inherited votes, writer exclusion
and resource ownership are still missing; this API cannot provide them.

## Scope

The only IDs are the saved GPU CNOC route `SH2/SN15/SN3` and DDR route
`SH0/MC0/ACV`. Addresses, units, widths and VCDs come from the mapped command DB,
not constants. Callers cannot pass arbitrary addresses, message IDs or prebuilt
commands. The API accepts **encoded 14-bit X/Y components**, not AB/IB or an
interconnect aggregation shortcut. Each component must not decrease, each
included request must raise at least one, and no saturation is performed.
ACV is only X=0,Y=0->8. Unchanged resources are omitted by the real coordinator.

All inputs and callbacks are copied before validation. The original owner must
prove stable DB/mapping, inherited or previously ACKed current components,
exclusive apps-DRV/VCD writers, no unknown pending VCD commit and deep-idle
exclusion. `validate` guards DB reads before and after; the sticky failed-read
latch prevents later DB accesses after revocation. `current` binds each decoded
resource to the real ledger at every TCS access. Callbacks must not sleep,
recurse or free resources needed to report terminal retention. **There is no
production implementation of these callbacks.**

The backend requires RPMh's existing initialized RSC/TCS/DB mappings. It does
not call `rh_map()` unguarded or use idle payload contents to infer ownership.
The inherited DB and RSC geometry still require a real platform coordinator.

## Shared Sender

ARC and BCM use `cepheus_rpmh_active_private.h` in the same translation unit,
sharing touch power's real `rh_lock/rh_poisoned`. This removes the duplicate
single-command implementation rather than introducing a second hardware lock.
ARC still sends exactly one WRITE_ACK command; its read lease now runs both
before and after each access. Touch's original VRM/XOB sender is unchanged.

BCM stable-sorts decoded resources by VCD and sets COMMIT, response request and
WAIT only at each VCD's final command. At most six commands fit one ACTIVE TCS;
the runtime geometry is checked first. The pinned downstream fabric similarly
merges whole VCD groups with `tcs_cmd_n_shrink()` up to `MAX_RPMH_PAYLOAD`.
The mailbox's completion predicate requires ISSUED for each command and
COMPLETE for requested responses. Our sender additionally requires a **fresh
IRQ**, idle state and unchanged full payload/control before cleanup.

All eight normal TCS banks are scanned before any store and again after the
chosen IRQ source is masked. Any armed BCM address is refused, even outside
the requested route: a foreign command may have a pending VCD dependency. This
does not inspect hidden TCS or prove pending BCM state is globally empty.
No sleep/wake programming, RSC reset, vote-off, retry or automatic recovery.
The second scan must select the same ACTIVE slot. If the original slot becomes
busy while another slot is free, the request is refused instead of being
redirected.

Attempted stores poison the shared transport first. Clean ACK and slot cleanup
clear trigger then mode, clear only owned enable/wait/IRQ state and restore
only the selected IRQ-enable bit. A lost owner, readback mismatch, corrupt
payload, missing command completion or timeout leaves state intact and reports
`finish(keep=1)` once. On success `finish(keep=0)` reports clean slot completion,
**not permission to release live rail/clock/bandwidth ownership**. The coordinator
updates its current-vote ledger only after success; partial/uncertain writes
cannot be interpreted as an ACKed new ledger.

`finish` receives a sender-owned read-only lease with the selected slot, exact
six-command snapshot, WAIT/enable/control, per-command status, IRQ observations,
IRQ-enable before/masked/final, attempted-store count and terminal state.
`CLEAN` is ACK plus owned-slot cleanup; `UNCERTAIN` is any attempted store
without a clean terminal path. The owner must check the snapshot against its
own request and retain on uncertainty; the pointer is not an ownership grant.

## Verification

Pinned downstream reference remains
`36f369fdb77319a2984a48b89f025ac3e5f97386`. Source checks bind clean mailbox,
fabric and arbiter files to COMMIT/response/WAIT/command-status rules; source
hashes are recorded. This is not running firmware identity or hardware proof.

Final local receipt:
`out/bcm-active-local-20261110-final-v2/receipt.json`.
Final four-step local preflight:
`out/bcm-active-preflight-20261110-final-v2/receipt.json`: all four steps PASS.

- 1977 BCM owner boundaries revoke both mapped DB/TCS apertures with
  `PROT_NONE`; no later access occurs. All 29 store failures retain uncertainty.
- Independent fixed offsets/data verify all 29 stores, stable VCD sorting,
  exact commit/WAIT/response bits, unrelated aperture preservation, single
  route/ACV/max-14-bit cases, reversed input and copied input/callback mutation.
- All-bank foreign BCM and late conflict, missing fresh IRQ/ISSUED/COMPLETE,
  payload/control corruption, bad metadata/current/context/lock, overflow,
  decreases/no-op/duplicate IDs and shared ARC poison rejection are tested.
  Fixed-slot diversion after IRQ mask is also refused, and finish snapshots are
  checked for exact commands, slot, IRQ restoration and terminal state.
- ARC's revised 153 lease boundaries/14 store positions still pass.
- ASan/UBSan, actual complete native RPMh AArch64 compilation/assembly/static
  analysis, host static analysis and staged-header-only native compilation pass.
- Both build-copy manifests, staging scripts, boot-no-call and kernel-symbol
  checks now include BCM. Local full preflight checks 69 host programs and
  seven production units; it is not a complete XNU compile/link.

Early local receipts are historical, not the final test/tool identity. The
first direct fixture compile rejected test-only `assert` syntax; fixing its
parentheses did not change production. Explicit pinned patch paths were added
after detecting the local-default path is absent in the CI checkout layout.

Full XNU CI `42819762105` at `02f4077` completed successfully on 2026-11-10
at 21:23 CST. The downloaded kernel and build log independently pass
`out/kernel-verification-ci-42819762105.json`: 17451096 bytes, SHA256
`a0170217a28daa09a639185135a74bfb5315d87560fa9c822289e99ce6d56611`,
86 core entrypoints, 22 memory APIs, no unresolved symbols and exact source,
ordered-touch, PA64 and native SCM capture checks. It contains this BCM and
revised ARC batch; the older `aa585f5` kernel does not. No packaging or boot
occurred. No new hardware test is requested for this building block.
PAS13=false, supported=false rejection and CP halt remain unchanged;
scratch/fence and Metal are unrun.

The locally accepted [core exit-tail diagnostic](rpmh-exit-tail.md) is now a
software-request collection building block. It has no DarwinBoot/XNU consumer
or phone evidence, and does not supply this sender's production owner or ACK.

Next: hardware-diagnostic integration, genuine final inherited ARC/BCM ledger, exclusive writer/deep-idle
coordination, then actual resource backend binding. Parent measurement
admission/provenance, remaining clocks/GDSC, PAS13/secure PA/crypto and SMMU
remain open; successful host sender tests do not close any of those contracts.
