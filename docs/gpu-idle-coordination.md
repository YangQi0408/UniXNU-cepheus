# Scoped GPU Idle Coordination

2026-11-11: `0089-cepheus-gpu-idle.patch` adds real SWEET XNU idle hooks
and a boot-lifetime one-shot hold/check/finish API. No startup caller acquires
the hold. Idle bookkeeping does run on the normal path; its timing and
phone behavior are not yet accepted. No boot arguments, `wfi`, `idle_enable`,
GPU MMIO, SMC, RSC commands, resource requests or CPU-record mutation changes.

## Covered Boundary

An atomic word contains eight logical-CPU reader bits, BLOCKED and sticky
FAULT. Entry and blockade linearize through the same seq-cst word. A holder
cannot report success until every pre-existing reader really exits; new
entries are refused. There is no inference from a sent IPI or a saved timer.
Duplicate/out-of-range entries or unmatched leaves latch FAULT and BLOCKED.
Eight is the SWEET board's compile-time maximum, not a claim that all cores
are live. This does not read/dereference asynchronously retiring CPU records.

The patched `cpu_idle` acquires before WFE recommendation/timer checks,
notifications, cache idle entry and WFI. Both early context-load returns
release. Normal `cpu_idle_exit(FALSE)` releases after exit notifications.
Reset entry `cpu_idle_exit(TRUE)` is not a reader release. The native private
header rejects `HAS_RETENTION_STATE`: the current non-Apple SWEET path returns
from WFI without reset; a retention-reset platform needs a different contract.

`machine_idle` also wraps its preceding IRQ-unmasked WFE recommendation.
It latches the entering logical CPU for release even if the host-modeled
continuation migrates. It releases before fallback to `Idle_context`;
`cpu_idle` acquires again, so a blockade in that interval denies WFI.

Hold requires interrupt-enabled, non-interrupt, zero-preemption-depth thread
context. It blocks new readers and waits passively at most 10 ms/10000 polls.
Backwards/stalled time or timeout is rejection, not drainage. Since this API
has no GPU effects, a clean timeout unblocks while pre-existing readers finish;
FAULT remains blocked. A successful hold returns token 1 only once per boot.
Concurrent/repeated attempts fail; output is unchanged on rejection.

`finish(token,1)` permanently retains the hold, including uncertainty after a
resource effect. A later release cannot undo retention. `finish(token,0)` only
releases an unretained held gate with no fault/readers. Invalid context/token
or an unexpected state does not drop the hold. Quarantined GPU effects can
therefore retain this boundary without pretending teardown succeeded.

## Not An Owner

This is CPU idle-path exclusion, not thermal supervision, a retained resource
ledger, a completed ARC/BCM request or global writer proof. It does not exclude
hotplug/suspend/debugger sleep assembly, secure/EL2 execution, firmware runtime,
other DRVs, DMA or inherited RSC solver/sleep/wake triggers. Other idle APIs
and the applied build must still be reviewed. A mapped resource owner remains
unimplemented. Do not use the token alone as `owner.validate` success or unhalt
CP. PAS13=false, supported=false and CP-halt rejection remain unchanged.

## Local Evidence

`scripts/test-gpu-idle.py` runs:

- 10000 real pthread reader/block races with eight readers and seq-cst checks.
- Native one-shot/context/output/drain/timeout/stalled-time/retention fixtures.
- Actual applied read-only-reference `cpu_idle`, `cpu_idle_exit` and
  `machine_idle` bodies, host/ASan/UBSan, covering early returns, WFE/WFI,
  reset entry, modeled migration, held blockade and normal fallback.
- AArch64 production-private compile/assembly with real exclusive atomics and
  no GPU/MMIO/SMC/WFI/WFE instructions, and warning-free Clang analysis.

The reference is the older `../kernel-test/xnu`, not the current CI build tree.
Missing reference is an explicit skipped source check for CI's pre-build step;
the post-build `verify-gpu-idle-kernel.py` requires actual applied source and
five linked native entries. The existing CI `42860421403 @d22a054` does not
contain these idle hooks and cannot accept them. No phone boot has occurred.

Earlier source-harness v1/v2 failed because its notification double incorrectly
required an idle reader on reset entry. The corrected fixture treats reset
notification separately; failure outputs are retained, not relabeled PASS.
No production warning suppression was added.

Accepted focused receipt: `out/gpu-idle-local-20261111-accepted/receipt.json`,
SHA256 `203b22861d28ddef7834e184321f18c0e43d43d6785a25021db0e3e627a8a329`.
The older scheduler reference's `idle_thread` disables preemption before
`processor_idle`/`machine_idle` and reenables afterwards; the source-body test
also models migration rather than depending on that source assumption.

Final full local preflight: `out/gpu-idle-preflight-20261111-final-v2/receipt.json`
PASS, SHA256 `35f198c6c0c1a40c278dbc65602d40e2aeff05437bbe8d4e123ff46b6599bd0e`.
All four prerequisites, 77 host programs and seven production analyses pass.
The earlier final-v1 preflight used the then-unfixed reset notification fixture;
its FAIL is retained. The final-v2 code did not change while its checks ran.
