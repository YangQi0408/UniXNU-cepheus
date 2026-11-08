# SoC delayed basic probe: offline preparation

Updated: 2026-11-08. Hardware status: NOT TESTED. The user chose to preserve
the current failed runtime and continue offline; no phone operations here.

## Purpose And Boundary

The old basic SoC thread waits 45 seconds before OSM/GCC/GPUCC/TSENS diagnostics;
the touch thread waits 60 seconds. The new opt-in adds `cephsocdelay=240` to
the basic `cephsoc=1` profile. This is a 240-second wait from the SoC thread,
not a guarantee of exactly 240-second system uptime or completed user acceptance.
Default 45-second behavior is retained. No new hardware reads, recovery writes,
scan commands, resets, calibration, power votes, DMA or secure calls are added.

The kernel rejects any other delay, invalid `cephsoc`, or presence of
`cephfuse`, `cepharc`, `cephgx`, `cephscm`, `cephboost`, or `cephmem`, including
values of zero, before any SoC probe. Packaging additionally requires GPIO touch
diagnostics and explicitly validates the basic-only delayed profile.

Only the delayed profile emits `CEPHEUS SOC` scheduled/begin/baseline-end markers
with kernel uptime milliseconds. Host UDP receive timestamps remain distinct
from kernel timestamps and physical touch times.

## Build And Packaging Gate

Production guard/thread tests run with RAM-only probe/clock stubs. They check
default45, fixed240, rejected configurations with no probes, wait/probe ordering,
all 26 baseline samples, phase order, and no optional probe or SCM execution.
Packaging guard tests and ready-argument tests enforce the same isolation.
Existing SCM serialization/query tests remain in place. All are wired into CI
and the full offline suite. Build and image identities will be recorded below
only after real compilation and independent payload verification.

Full offline suite: 37 PASS / 0 FAIL / 0 SKIP in
`../logs/gpu-offline-socdelay-20261108-r2/receipt.json` (pre-commit dirty tree).
The first run, `../logs/gpu-offline-socdelay-20261108/receipt.json`, is retained
as FAIL: the GX host test's neighboring-function extraction included the new
delay helpers. Its extraction endpoint was corrected; no GX probe changed.
The timeline now records kernel phase timestamps separately from UDP receive
timestamps, with 5 host tests passing. Kernel build remains pending.

Required pair: a new-kernel touch-only control and the same-new-kernel delayed
basic SoC candidate. The strict delta receipt must prove identical kernel,
ADT, other payload files and all TOML fields except adding exactly
`cephsoc=1` and `cephsocdelay=240`. Older PASS images are not overwritten.

## Next Hardware Gate

No automatic reboot, recovery or switch from the preserved failed runtime.
User must explicitly return the device to the test state. Test the new-kernel
touch-only control first; main screen, center drag/taps, edge/two-finger touch
and power blank/wake must be accepted for that exact boot.

Then independently temporary-boot the delayed candidate with a 600-second
capture. Record the prior OS/power/USB state and a no-contact initialization
window. Obtain central-touch/user-UI PASS before the `begin` marker, then repeat
central actions during baseline sampling and after `baseline-end`, with separate
user feedback and action timestamps. Automatic delay expiry is not an acceptance
gate: if there is no confirmed pre-begin PASS, mark that comparison inconclusive;
do not attribute its failure to the later probe or automatically recover.

Touch RAW has a bounded 128-event budget, so later phases may have health logs
but no remaining RAW. Do not infer phase-specific central event absence after
that budget has been exhausted. Require user feedback and health/HID deltas;
missing UDP phase markers or incomplete windows also limit the conclusion.

This experiment tests timing association for the grouped basic probe only. It
does not isolate OSM versus GPUCC versus TSENS, establish inherited-state
equivalence across boots, or prove the intermittent touch root cause.
