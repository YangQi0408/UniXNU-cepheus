# Scoped RPMh Writer Coordination

2026-11-11: real participating XNU apps-DRV admission is added inside the
existing `sweet_rpmh.c` translation unit. This is not a resource owner or
completed inherited request ledger. No startup caller acquires a reservation.
There is no new MMIO, SMC, vote, sleep/wake programming or CP operation in
hold/check/finish. PAS13=false/supported=false and CP halt are unchanged.

## Actual Boundary

`cepheus_gpu_rpmh_hold(idle_token,&token)` requires normal interrupt-enabled,
zero-preemption-depth thread context, the real scoped idle token, initialized
RSC/TCS/DB mappings and an unpoisoned transport. Admission uses the actual
`rh_lock`, excluding an in-progress participating legacy writer. A successful
reservation returns token 1 once per boot. Admission refusal is terminal for
this API but leaves legacy writer permission unchanged, without effects.

The generated actual `sweet_rpmh_vreg_enable` and `cepheus_touch_power_enable`
check reservation admission under `rh_lock` before mapping/lookup/send. While
HELD, RETAINED or FAILED they return EBUSY, with no hardware callbacks. OPEN
and RELEASED preserve legacy behavior. No touch reset or automatic recovery
is introduced. The old source comment claiming a sole owner/deep-idle policy
is replaced by an explicit participating-transport limitation.

ARC/BCM copied owners carry `rpmh_token`. Before any DB/TCS access the adapters
check the real scope as well as their existing resource-specific guard. The
first attempted transport store permanently retains the CPU-idle token and
apps-DRV reservation BEFORE touching MMIO. Callback reentry cannot release
them because the actual transport lock is held. Nonzero effects, uncertain
transport poison or invalid idle state never unblock legacy writers.

`finish(token,0)` only releases an unretained, effect-free reservation; the
caller separately owns its still-held idle token. `finish(token,1)` retains
the reservation and idle token. A failed idle check is sticky even if a later
test callback would return success. Invalid context/token cannot release.
Management calls and completion callbacks must not recurse into scope APIs.

## Separate Missing Conditions

This excludes participating XNU apps-DRV callers, not other DRVs, firmware,
secure/EL2 writers, hardware RSC solver/sleep/wake triggers, DMA or a global
writer census. It does not bind current CX/MX requests, BCM/VCD state, stable
command DB ownership, clocks, TBU access or thermal health. Keep every existing
ARC/BCM owner validation and ACK/cleanup rule. No current=0 or fake owner is
created. Native GPLL0/power/CM3/PDC/SMMU/upload serialization is still separate;
a platform owner must coordinate both domains without callback deadlocks.

## Local Evidence

`scripts/test-rpmh-scope.py` extracts actual transformed legacy function bodies
and exercises hold/refusal/release/retention with host and ASan/UBSan, 1000
pthread readers, sticky idle failure, context/token/mapping/poison refusal,
and zero map/lookup/send callbacks while held. It also compiles/analyzes the
complete transformed RPMh plus actual ARC/BCM AArch64 body.

`out/rpmh-scope-source-20261111-final-v2/receipt.json` and
`out/rpmh-scope-adapters-20261111-final-v2/receipt.json` pass. The latter runs
the existing per-access PROT_NONE/fresh ACK/shared-poison/copying regressions
through the actual new scope implementation, plus staging/native/analysis.
Idle and hardware callbacks are synthetic in these tests, not a runtime owner.

Focused source receipt SHA256:
`36dcbb395dc2e9720d3fdd7bcd5a5dcf0a200ff6ff7502d849daadc0ebccd44b`.
Adapter receipt SHA256:
`394709c6c90b354a20180fb5385dad313adcdca4c9fd3b3546db06ea46889448`.

Earlier adapter v1 missed the two new headers in its staging manifest; the
failure is preserved. v2/final receipts are separate. Full preflight and full
XNU link of this later batch require their own acceptance. CI42867280759
@91a5ec1 does NOT contain this reservation component and cannot accept it.

Final preflight `out/rpmh-scope-preflight-20261111-final-v2/receipt.json` PASS:
four prerequisites, 77 host programs and seven production analyses. SHA256
`f31e8502e85a410a1c3d2c55d404df8d77fe43b33c61dcb8d21781a8f373ed02`.
The earlier v1 also passed but a comment/startup-negative assertion changed
while it ran; final-v2 verifies the final source without those concurrent edits.
