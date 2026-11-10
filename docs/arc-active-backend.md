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
after the fix; the macOS macro-guard fix was tested separately.
**Complete XNU build/link and independent artifact acceptance passed** for
`bba0314` in run `42792610794`; see the final receipt below. No boot
image was made and no phone operation occurred. The accepted SCM-pair boot
remains running; GPU scratch/fence, Metal and PAS authentication remain unrun.

Next: a genuine post-EBS inherited-vote ledger and exclusive owner must bind
this transport to power `prepare/vote/finish`; a synthetic callback is not an
admission path. Do not schedule another touch/SCM/memory test for this module.

At 17:37 CST, implementation commit `7c80907` was pushed and full XNU job
`42792076835` started at that exact commit. Source staging passed; compilation,
linking and compiled-artifact acceptance were pending at that observation. Local
tracking: `out/arc-active-ci-42792076835.json`. No boot is planned for this
unhooked transport by itself.

At 17:40 CST, that job was confirmed **FAILED before XNU compilation**:
macOS headers already define `__unused`, and the RAM test redefined it under
`-Werror`. An `#ifndef` guard fixes the test without changing production code.
Linux checks with both absent and Darwin-style existing macros pass. The old
job is not build acceptance; a new fixed-commit run must replace it.

Replacement job `42792610794` at macro-fix commit `bba0314` was dispatched
at 17:40 CST and completed **SUCCESS at 17:51:44 CST**. Production ARC code is
unchanged from `7c80907`. Tracking: `out/arc-active-ci-42792610794.json`.

Independent local acceptance at 17:52 CST:

- Kernel `inputs/ci-42792610794/kernel.macho`, 17433272 bytes, SHA256
  `00abd9206e8b086fd924ca4a5bfde26ceb2b24fe61ba80550978ae88816a9690`.
- Exact CI/device commit, pinned ios-sweet identity, production XNU sources,
  82 core symbols including `cepheus_gpu_arc_raise`, 22 memory APIs, no
  unresolved symbols, ordered touch driver and 64-bit cache callback passed.
- Receipt `out/kernel-verification-ci-42792610794.json`; immutable build log
  `../logs/arc-active-ci-42792610794/build.log`, SHA256
  `0748cf7627a34c430bbbcf7ba40ffdf8e3fb2ed5e29af2661daa25138e573f8c`.
- Linked ARM64 listing `out/arc-active-linked-ci-42792610794.txt` retains the
  owner calls, shared lock/poison accesses and actual MMIO barriers. A full
  disassembly search found no named direct branch to the transport entry;
  this does not rule out indirect references or prove runtime behavior.

The fixed kernel is a verified building block, not an executable GPU profile.
No image packaging, boot, PAS authentication, or GPU execution followed.
