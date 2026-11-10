# ARC ACTIVE Native Transport

2026-11-10: the missing ARC write transport is now implemented in the actual
`sweet_rpmh.c` translation unit. It is a private kernel building block, not an
owner implementation, startup hook, or GPU execution candidate.

## Contract

`cepheus_gpu_arc_raise()` accepts only `cx.lvl` and `mx.lvl`, requires a strictly
increasing legal command-db table index and thread context, and shares the
existing `rh_lock` and boot-terminal `rh_poisoned` with touch power. Data is a
hardware level index, not millivolts or abstract VLVL. No BCM normalization is
reused, so Android/UEFI rounding differences are unaffected.

The platform's `ceph_arc_active_owner.validate` must prove the current apps-DRV
vote, exclusion of foreign writers, and deep-idle exclusion throughout every
read/write. There is still **no production implementation of this callback**.
An empty TCS is never used to infer `current=0`, inherited ownership or a lease.
The caller must preserve MX-before-CX ordering and update its owner ledger only
after success. Uncertain results retain the rail lease; there is no retry or
vote-off recovery. This does not resolve PAS13=false, PAS memory permissions,
crypto clocks/bandwidth, clock/GDSC ownership, SMMU, or thermal supervision.

The adapter checks all eight normal TCS banks for armed commands targeting the
same resource, then uses only one empty idle ACTIVE slot. It masks only that
slot's interrupt source. All payload fields are read back; success requires a
fresh IRQ absent before trigger, idle state and command ISSUED+COMPLETE. Stale
command status or a busy transition alone cannot satisfy completion. The
payload, enable/wait/control fields are checked again before cleanup.

Any uncertain store, lost lease or ACK timeout poisons the shared transport and
leaves the slot intact. A successful path clears trigger then mode, only its
command enable/wait and W1C IRQ bit, then restores only its IRQ-enable bit.
Sleep/wake/hidden TCS, unrelated payload/IRQs, global reset and physical rail
verification are outside this API. ACK is not proof of physical voltage.

## Verification

Local final receipt:
`out/arc-active-local-20261110-final/receipt.json`.

- RAM tests inject all 70 lease boundaries and 14 uncertain writes, armed
  conflicts in all eight banks, stale completion, missing fresh IRQ, corrupted
  payload/control, invalid metadata/context, and shared lock/poison rejection.
- ASan/UBSan, 66 host programs and seven existing production-unit static
  analyses passed. Native ARC/RPMh analysis passed separately.
- The actual pinned RPMh functions plus the adapter compile/assemble for
  AArch64 with their real MMIO barriers. Only platform headers/declarations are
  substituted; no firmware/MMIO/SMC is executed.
- ARC staging/application tests passed six cases. Integration passed nine
  cases with the optional source case supplied by local preflight.
- Both local and CI copy manifests, stage scripts and the compiled-kernel
  verifier include the new private files/symbol. No startup call was added.

The earlier `out/arc-active-local-20261110/` receipt is preserved; it predates
the copy-manifest fix and the additional control checks. Do not use it as the
final source identity. The final receipt records a dirty source tree; later
documentation edits and the explicit CI patch-directory fix are not claimed
as part of that run. The native check was rerun against the staged directory
after the fix. **Complete XNU build/link has not run for this change.** No boot
image was made and no phone operation occurred. The accepted SCM-pair boot
remains running; GPU scratch/fence, Metal and PAS authentication remain unrun.

Next: a genuine post-EBS inherited-vote ledger and exclusive owner must bind
this transport to power `prepare/vote/finish`; a synthetic callback is not an
admission path. Do not schedule another touch/SCM/memory test for this module.
