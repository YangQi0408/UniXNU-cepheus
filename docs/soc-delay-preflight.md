# SoC delayed basic probe: offline preparation

Updated: 2026-11-08. Control hardware round PASS; delayed candidate current
UI/touch/power-wake PASS, but phase comparison INCONCLUSIVE.
The user returned the device to fastboot and authorized testing after the offline
stage. Historical offline preparation and preserved-failure boundaries are below.

## Delayed Hardware Round: Inconclusive Phase Capture

2026-11-08 14:02: verified serial67eea163/product=cepheus, image/kernel SHA and
strict pair delta; receiver and exact-MAC watcher ready before temporary boot.
Sending1.590s/booting0.126s OKAY, exit0. User confirmed direct accepted control
iOS -> physical power/volume-down -> fastboot and no contact during initialization.
No partition flash, agent reboot/reset, scan resend or power recovery.

Initial central3-drags/5-taps request received "现在正常", first observed by host
at14:05:07. Current central retest received "正常", observed14:08:45. Final central
and power blank/wake retest received "正常", observed14:11:07. Current main screen,
central touch and power-wake-touch are accepted for this boot; final response is
combined, not separate per-action timestamps. **These are NOT confirmed
pre-begin/during/after-baseline acceptances.** No SOC phase markers were received,
and the last UDP packet preceded the first host observation of the initial reply.

Capture planned600s, stopped by signal at469.530239s,14:09:39.384386 after the
execution environment reported a server restart. Exact signal sender unrecorded;
both capture and owned watcher absent after restart.314 packets/103834B,
log SHA256 `e89b06c2f1bd1486047be778a66c7419de3769edf9c666a5f00572725a85dfef`.
Only packets14:03:02.550769..14:04:28.066009 were received. Host journal shows
ECM reenumeration14:04:28 and14:07:24; new interface UP/LOWER_UP with correct
host address, but0 valid RX packets and growing RX errors. User reports no
deliberate cable/replug/OS change, but suspects the cable. Cause UNKNOWN; do not
attribute to cable or infer a phone reboot from enumeration alone.

Saved log has ordered ST-FTS initialization,12 HID down/up each, RAW128 reached,
40 complete batches/126 events production replay0 differences; n127..128 partial
tail omitted. Prefix down9/move106/up8 is not whole-round delivery. Only one
health record, polls1000/events3/errors0, predates central actions; it is not a
post-probe or final-state counter. No scheduled/begin/baseline-end, sample25 or
basic-probe records received. driverkitd sig9 and two PosterBoard sig5 userspace
crashes present; no captured kernel panic does not exclude an unseen tail.

Evidence `../logs/socdelay240-b2b99e7-live-20261108-1401/`: preflight/fastboot,
capture/packets, action timeline, host observations, result, replay and validated
receive timeline. Receive-timeline5 and touch-replay11 host tests PASS.
Result SHA256 `07f10791d1ac6229a2d23ac922f9af9968bb36459c1afecef5e5a1d8b0c5bc4f`;
action timeline SHA256 `e1039cef21d40f2c46350138c81dab5483c1224b1ef40e45ed27d0c14e1dc6a0`;
host observations SHA256 `76ad0e53ff01c89f54ec11249aab8ba7167222d2ae2ea311fe1d69ba0bffdd33`.
**Phase comparison INCONCLUSIVE; actual probe execution UNKNOWN.** Wall-clock
boot age or missing UDP markers cannot substitute for kernel phase timestamps.
GPU/Metal acceleration remains unexecuted/unaccepted; intermittent root cause
UNKNOWN. Phone current accepted state preserved; no automatic next boot.

Next gate: a reliable data cable, explicit user authorization to manually enter
fastboot, repeat the exact verified delayed image with uninterrupted600s capture,
and separately timed pre-begin/during/after acceptance. If state equivalence is
not preserved, repeat the control too. Do not add fuse/ARC/GX/SCM/boost/memory or
GPU startup until this phase capture is complete. Do not repair USB live by reset.

## Control Hardware Round

2026-11-08 13:52: serial67eea163/product=cepheus in fastboot, SHA and packaging
rechecked, host exact-MAC watcher and420s UDP receiver ready before temporary boot.
Sending1.584s/booting0.126s OKAY, exit0; no partition flash. User reports entering
fastboot from previous failed iOS via physical power/volume-down, with no
intermediate Android reported. No agent reboot/reset/scan resend/recovery.

After center5 drags/20 taps, edge/two-finger and power blank/wake request, user
replied "都正常了": main screen/touch/power-wake-touch PASS for this boot.
589 packets/157985B/420.001692s, log SHA256
`d07228282f56bbd272891a1b76650c9765b7e8d36b99be0df1eb83b015bc4700`.
Evidence: `../logs/touchcontrol-b2b99e7-live-20261108-1347/result.json`, capture,
packet timings, action timeline, replay and receive-timeline. Last health18000:
events1179/hid_calls1172/errors0/faults0/active0;18 health records. HID down/up55
each, power down/up6 each, blank clear706/710/891us. No SoC/GPU/SCM markers.

RAW128 budget reached;26 complete batches/120 events, production and ordered
model0 differences. Truncated batch n121..128 omitted. Prefix down3/move112/up2
is not entire-round delivery; no later RAW after budget. No-contact instruction
not separately confirmed. UDP/one-boot limits remain; intermittent root cause
UNKNOWN. Receiver completed, own watcher stopped, ECM still present. At that
checkpoint delayed candidate was unbooted; its later round is recorded above.

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
timestamps, with 5 host tests passing.

Committed source: `b2b99e7f84cedd6e7d4c6cb058363149786d1a00`, pushed to
`feat/adreno640`. Clean-tree full rerun also passed 37/0/0:
`../logs/gpu-offline-socdelay-b2b99e7-20261108/receipt.json`, SHA256
`63c8edcc2ba81d21f319c55614f7da637a3631dbd03df179f9dbe61804d758a8`.

CI `42450429814` completed successfully on this commit. Pinned ios-sweet
`45263bee51df4a0f55cdf9c649af886399488ddb`, dxb
`663906528d1cd5ea8fde7de6f16de73db9511549`; all critical patches, staged source
cmp, 75 GPU core/13 memory API symbols, no unresolved symbols, ordered-touch
marker and 16KiB PA64 checks passed. Noncritical pre-existing skipped patches
remain recorded, not silently described as all patches applied.

Kernel: `../inputs/ci-42450429814/kernel.macho`, 17414536 bytes, SHA256
`e9f8f3f735e2ab6d3f52fe2817ec51b898502b6f79130687f3b63c214ebd4978`.
Local evidence: `../logs/ci-42450429814/verification.json` and
`soc-thread.disasm.txt`; inspected linked 240000/45000 ms waits and guard/clock
calls corroborate code generation, not exhaustive control-flow or hardware proof.
Verification receipt SHA256:
`b4a5dd08c57f371ed98ff69764100d030ba203b7bd33322ac35e78d745803d1c`.
Build log SHA256:
`408529329292f6f2e89b0d7462139dad414f9e9a147492628cb11de8e417bbc9`.
Touch source unchanged SHA256:
`672ce030f38efc4d8ef4764061f318237e9b67723f9fc27103edc2abb5e3700b`.
SoC source SHA256:
`7abbb569c16d35b9cc19c873aed9369d9702e403ade66ae8395d577fc38a0c57`.

Required pair: a new-kernel touch-only control and the same-new-kernel delayed
basic SoC candidate. The strict delta receipt must prove identical kernel,
ADT, other payload files and all TOML fields except adding exactly
`cephsoc=1` and `cephsocdelay=240`. Older PASS images are not overwritten.

## Completed Image Pair

Both images are 70680576 bytes, built with real mtools in the existing
network-none, UID1000, cap-drop-ALL/no-new-privileges build container.

| Image under `../dist/` | SHA256 | Hardware |
|---|---|---|
| `boot-cepheus-touchcontrol-b2b99e7.img` | `afdf72c6d4159d52d39dc687cfa733dc772c3cc5b717ff4cbfdc213302f65f6c` | 2026-11-08 control PASS |
| `boot-cepheus-socdelay240-b2b99e7.img` | `7e082de680a9ecd09d4c6fe698ecd6b7d65906eb1b08a50c18692ba7b142747b` | 2026-11-08 current touch/wake PASS; phase INCONCLUSIVE |

Control receipt: `boot-cepheus-touchcontrol-b2b99e7.json`, SHA256
`15dcfb4372e52074d85b4effae2610b2d9cca442ab91dffcad272c27184ffd89`.
Compared with the prior PASS touchordered-60a2dc5 image, control packaging
preserves ADT/other payload, firmware/header and separately checked all TOML
fields; only the new kernel changes. This does not inherit prior boot acceptance.

Strict pair receipt: `boot-cepheus-socdelay240-b2b99e7-delta.json`, SHA256
`dd15fd30fee303d59bdd3ced35b9e573c40c4c3867b9fc174142a214c777aba7`.
It proves the same kernel/ADT/other payload/TOML fields, adding only
`cephsoc=1 cephsocdelay=240`. Firmware/root were not flashed, and no phone was
used for this offline work. The retained failed runtime was not rebooted.
These are historical offline-stage statements; later authorized temporary boots
are recorded above. Original packaging receipts remain packaging-only snapshots.

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
