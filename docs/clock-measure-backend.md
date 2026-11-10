# GPLL0 Counter Measurement

2026-11-11 follow-on: the native wrapper now privately publishes only a
successful actual counter record, under its existing GPLL0 lock. Returned
output mutation cannot change it and failure never publishes. Native power
requires that same record and retained live parent before mapping and around
every external resource guard. This is a necessary nominal-XO consistency
check, not calibrated 300MHz proof; see `power-native-backend.md` for bounds,
local evidence and the separate missing ownership requirements. The new
publication/power binding is not contained in CI42881254237 @d5bf1dd.

2026-11-10: `cepheus_gpu_gpll0_measure()` is now a private native building block
over the **same mapped GCC aperture and serialization as GPLL0 hold/check**.
No phone operation, frequency measurement on hardware, GPU startup or boot
packaging has occurred. A successful host fixture is not actual frequency.

## Source Rules

The local pinned Xiaomi SM8150 reference is still
`36f369fdb77319a2984a48b89f025ac3e5f97386`. Extended `test-gpll0.py --android`
checks the relevant clean files and records hashes for `clk-debug.c` and
`debugcc-sm8150.c` in addition to the parent-hold sources.

The fixed direct GCC source is `gcc_gpu_gpll0_div_clk_src`, selector0x167,
primary divider1. Registers are GCC+0x62000 mux, +0x62004 divider, +0x62008 debug
branch, +0x43008 XO4 branch, +0x62038 control and +0x6203c status. No recursive
GPUCC debug mux or unrelated clocks are touched. The only programmable fields
are mux bits9:0, divider bits3:0, branch bit0, and fixed control values for the
0x1000 short and 0x10000 full reference windows and CNT_EN bit20.

The source samples both windows because an off clock can retain the same old
ring-oscillator count. DONE is bit25 and count bits24:0. Our implementation
requires DONE clear before each start and set after it, stable count/control
readback, and a duration no shorter than the XO4 window minus one tick.
It adds 25ms per poll, 100ms total and independent iteration bounds; downstream
loops do not provide these bounds. The native clock is `mach_absolute_time`
converted through `absolutetime_to_nanoseconds`, with real `delay(1)` pauses.

The full result follows the downstream count conversion:

```text
hz = ((count * 10 + 15) * 4800000) / (ticks * 10 + 35)
```

Results include raw counts, both rates, both elapsed times and count-resolution
quanta (1171Hz short, 74Hz full). Zero/equal/decreasing/saturated counts and
short/full inconsistency beyond the summed counter quanta are rejected.
The consistency gate is an added conservative diagnostic rule, not a derived
GPU admission rule. The nominal 4.8MHz reference is **not a calibrated XO**;
the API does not invent a reference-tolerance budget or round to 300MHz.
Its `full_hz` and the earlier `configured_div_hz` remain distinct.

## Ownership And Restoration

An actual independent debug owner must exclude all writers to these shared
words, provide config-access bandwidth, hold live XO and the original GPLL0
owner, and prevent deep idle. There is **no production owner implementation**.
Local API locking is not global exclusion. Neither idle counter contents nor
a RAM fixture is ownership evidence. The callback/context must outlive failed
accesses so terminal retention can still be reported without freeing resources.

Running/unknown counter controls are rejected before any write. Parent checks
run before setup, after sampling and after restoration. Every debug load/store
checks both owners; attempted stores mark terminal uncertainty first. Unrelated
software fields are preserved, and known branch status bits31:28 are neither
echoed back nor restored.

After two clean completions, the initial control, XO4/debug enable bits,
divider and mux are restored with exact owned-field verification. Previously
enabled branch bits stay enabled. Read-only counter data is not restorable and
is not written. Restoration does not release the GPLL0 parent's boot-lifetime
hold. `finish(keep=0)` runs once only after this clean restoration, including a
cleanly sampled but invalid count result. Any uncertain access, timeout, owner
loss or failed restoration leaves the current state intact and uses keep=1.
No cleanup retry, automatic vote-off or repeated measurement is available.

The existing power `prepare()` contract forbids hardware writes on failure;
this measurement writes shared debug hardware. Therefore it is deliberately
not hidden inside that callback. A future real platform coordinator must
account for parent/debug effects and provenance explicitly. No public API
accepts a caller's claimed measured-ready flag, and `parent_hz==300000000` has
not been weakened or populated from the nominal PLL register calculation.

## Verification

Final local adapter receipt:
`out/clock-measure-local-20261110-final-v2/receipt.json`.
Final four-step source/build preflight:
`out/clock-measure-preflight-20261110-final-v2/receipt.json`.

- The actual native adapter performs volatile stores on mapped test RAM;
  the hardware model reacts to those stores, not substituted read/write APIs.
- All 208 measurement lease boundaries revoke the aperture with `PROT_NONE`,
  preserving outputs, retaining uncertain effects and rejecting native retry.
- All 15 store positions, stale/stuck/early DONE, stopped/backward timer,
  parent lock loss, corrupted mux/control, off/saturated/inconsistent counts,
  active/unknown counters, bad callbacks and lock/context refusal are tested.
- Success preserves the rest of the GCC aperture, prior branch enable bits,
  0/0x1000/0x10000 stopped controls and existing desired mux/divider fields.
- ASan/UBSan, native AArch64 compile/assembly, native/host static analysis and
  staged-only native compilation pass. Local/CI staging and compiled-kernel
  symbol verification require this entrypoint; boot-source tests forbid calls.
- SCM native-policy tests pass after adding the new header/time declarations
  to their test-only platform preamble. PAS13 rejection and CP halt unchanged.

Earlier receipts remain historical. These local receipts are not complete XNU
link or hardware acceptance. No new touch/SCM/memory test is needed. GPU
scratch/fence, Metal, PAS authentication, true shared ownership and SMMU remain
uncompleted.

Implementation `6482292` was pushed and full job `42809502486` dispatched at
19:41:21 CST. It **failed before XNU compilation** at 19:44:54: Apple ASan
does not support `detect_leaks=1`. Host/staging and production static-analysis
steps passed; no kernel was built. The test script now disables only leak
detection on Darwin while retaining ASan/UBSan and Linux leak detection. This
does not change the production parent/counter code. The failure is not accepted
as a full build; a replacement run must prove compile/link/artifact acceptance.

## Accepted Full Build And Independent Fixtures

Replacement `42810544983 @aa585f57fab0a327198c9856a032270b270c8c30`
completed successfully at **2026-11-10 20:05:32 CST**. The downloaded kernel and
build log independently pass `verify-ci-scm-pair.py`: 85 required core symbols,
22 memory API symbols, no unresolved symbols, exact production-source identity,
ordered-touch/64-bit-PA checks and native SCM CPU capture/codegen.

- Kernel: `../inputs/ci-42810544983/kernel.macho`, 17450712 bytes.
- Kernel SHA256: `dd7d7339da56279976d8ca881a26419d943ccb3b6e5a6c2a14ef37c9128b4a83`.
- Acceptance: `out/kernel-verification-ci-42810544983.json`.
- Build log: `../logs/clock-measure-ci-42810544983/build.log`.

The follow-up changes **only the test and documentation**, not production.
The hardware fixture now uses its own explicit six register offsets, asserts
selector/divider/gates at counter start, and uses fixed raw counts and window
durations rather than inverting the implementation's conversion. Eleven exact
rate fixtures cover low counts, adjacent counts and 64-bit multiplication; five
valid pairs cover nominal 19.2/200/300/600MHz and the consistency boundary, with
the next count rejected. Non-300MHz diagnostic results stay valid and are not
GPU admission. All 208 lease failures and 15 store mutations still pass.

Local ASan/UBSan, native/staged compilation and static-analysis receipt:
`out/clock-measure-independent-20261110-v2/receipt.json`.
All four local kernel-preflight steps, including 68 host programs and seven
production-unit host static analyses, pass:
`out/clock-measure-independent-preflight-20261110/receipt.json`.
The initial independent run is retained as FAIL: one test-only expected rate
was 1Hz too low; correcting that constant required no production change.
The accepted kernel remains identified by `aa585f5`; this test-only follow-up
does not require another full kernel build. No boot image or phone operation
occurred, and this acceptance does not establish real clock ownership, actual
measured frequency or GPU execution.
