# GPLL0 Parent Hold

2026-11-10: private `cepheus_gpu_gpll0_hold/check` native entrypoints now map the
fixed GCC aperture and retain an **already calibrated, FSM-enabled, active,
locked RUN GPLL0**. They are not startup hooks, an owner implementation or a
GPU execution candidate.

## Source And Scope

Pinned local Xiaomi SM8150 downstream commit:
`36f369fdb77319a2984a48b89f025ac3e5f97386`.
`test-gpll0.py --android PATH` verifies this commit, clean relevant source files,
records their hashes and checks the concrete source rules:

- `gcc-sm8150.c`: TRION GPLL0 at GCC+0, apps vote GCC+0x52000 bit0;
  GPU divided gate GCC+0x52004 bit16, `clk_gate2_ops`, 500us delay;
  even divider USER_CTL bits11:8 encodings 0/1/3/7 = 1/2/4/8.
- `clk-alpha-pll.c`: FSM enable only votes and waits ACTIVE; non-FSM enable
  can reconfigure. Calibration can transiently require higher voltage.
  USER_CTL+0xc, STATUS+0x30, OPMODE+0x38, ALPHA+0x40, PCAL_DONE bit26;
  fractional rate uses nominal XO times L plus alpha scaled by 2^16.
- `clk-branch.c`/`clk-regmap.c`: gate enable is masked vote RMW plus delay,
  not a CBCR halt-status poll.
- `gpucc-sm8150.c`: divided parent selector6, 200MHz from 300MHz /1.5.

The implementation requires all three existing output-enable bits, refuses
offline/reset-FSM/update, missing calibration, unknown divider, unstable
snapshots and a configured divided rate other than 300MHz. No PLL mode,
L/ALPHA/divider/configuration, main GPU gate bit15, GPUCC, GX, reset, ARC or debug
measurement registers are written. It does not start an inactive PLL.

`configured_div_hz` is explicitly a **register-derived nominal rate**, not
measured frequency. It must not be used to satisfy the existing power
`prepare()` measured-parent requirement. `debugcc-sm8150.c` routes the divided
gate as selector0x167; actual measurement needs separate shared debug-register,
CXO and bandwidth ownership. That backend is still absent.

19:37 continuation: the separate bounded counter backend is implemented and
locally verified; see `clock-measure-backend.md`. Its production owner and
actual hardware observations are still absent, so this hold API's nominal
rate must still not satisfy the measured-parent admission contract.

## Lifetime

The platform must supply actual exclusion for both entire shared vote words
and PLL configuration, hold inherited CXO/rails and the mapping, and prevent
deep-idle transitions. There is still **no production owner callback**. Idle
register values do not establish that contract. Local singleton serialization
only excludes callers of this API, not firmware or other GCC users.

Every access validates the external owner before and after the volatile load;
stores validate immediately before the real write, use `dsb osh`, preserve all
unrelated bits and poll bounded readback. Both vote words and PLL configuration
are checked again across the complete attempt. The gate always waits 500us,
including adoption of an already set bit. Readback is not frequency measurement.

Attempted stores mark terminal uncertainty before the store. Lost ownership,
changed shared words, dropped writes, clock-state changes or timeout leave
votes intact and invoke `retained` once. Success also retains resources for
this boot. The callback/context must remain callable after access revocation;
revocation must not free resources needed by this retention notification.
No clear-vote, release, restart or retry API exists. Native attempts cannot be
retried even after a read-only rejection; subsequent check failure poisons the
held singleton. No caller supplies a boolean claiming successful measurement.

## Local Verification

Final adapter receipt: `out/gpll0-local-20261110-final-v2/receipt.json`.
Full four-step local preflight: `out/gpll0-preflight-20261110-final/receipt.json`.

- Real native adapter on mapped host RAM: only two permitted stores, unchanged
  remaining aperture, existing-vote adoption and 1/2/4 even-divider cases.
- All 93 hold and 32 check lease boundaries revoke access with `PROT_NONE`.
  Unchanged outputs, uncertain retention and native retry/lock/context gates.
- Dropped writes, unrelated vote mutations, lost lock, gate/divider changes,
  invalid source/status/rate, bad aperture metadata and delay failure.
- ASan/UBSan, actual native AArch64 compile/assembly, native and host static
  analysis, and stage-only native compile using copied headers pass.
- Local/CI copy manifests, staging and compiled-kernel symbol verifier include
  both entrypoints. Boot-source checks prohibit startup calls. Existing SCM,
  PAS and CP-halt policy is unchanged.

These are building-block tests, not actual clock, XNU link or GPU acceptance.
Earlier receipts are retained as historical runs, not final source identity.
No phone operation or boot packaging occurred. Accepted SCM-pair UI/touch/wake
remain the last reported hardware result; GPU scratch/fence, Metal and PAS
authentication remain unrun. Next: real shared owner/current-vote ledger,
parent measurement, remaining clock/GDSC, secure PA/crypto/PAS13 and SMMU.
