# UEFI/XNU PAS13 paired diagnostic

## Current preparation

On 2026-11-09 the kernel pair implementation was committed as `e5e7104`.
CI `42627950109` failed before kernel compilation because the GX host fixture
included the newly inserted pair helper. Commit `6ce4c7c` fixes that extraction;
CI `42632917379` has passed source checks, host regressions, static analysis,
native AArch64 codegen and KDK installation and is compiling XNU. Its final
kernel and hardware result are still pending.

Local DarwinBoot build:
`out/scm-pair-build-v2/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi`,
SHA256 `8ce496a6621101c3354dc96cf031fb7ac1dc2ca89775cccddf19a07c41f4051b`.
Local FV replacement: `out/scm-pair-repack-v4/scm-pair.fd`, 3145728 bytes,
SHA256 `7fd09e2c96b0f53cb8e608ed82bbd11ca694547f7c5da2d53c8ca928575d20f6`.
The FV receipt proves unchanged baseline reconstruction, 197 nested files,
only DarwinBoot changed internally and only its compressed wrapper changed
in the outer FV. The actual embedded EFI matches the compiled file.

The complete kernel prerequisite run passed locally with an explicit XNU
reference: `out/local-kernel-preflight-v2/receipt.json`. The actual staged ADT
parser, handoff policy/corruption checks, firmware codegen, nine paired-log
tests and end-to-end packaging fixture with ten rejection cases also pass.
The packaging fixture uses a synthetic kernel and is temporary; it is not a
bootable candidate. The exact final kernel still requires artifact verification.

## Diagnostic behavior

DarwinBoot queries the existing fixed INFO/PAS13 discovery policy once before
ExitBootServices. It records x0..x7 input/return, CurrentEL, MPIDR_EL1 and
CNTPCT_EL0 in a fixed 2656-byte transcript. After successful ExitBootServices
it finalizes the checksum and copies the transcript to the reserved ADT slot.
No protocol/function pointer crosses into XNU.

XNU's isolated `cephscmpair=1` profile waits 90 seconds, copies the ADT property
to aligned storage and validates the header, checksum and production policy
replay. It dumps the firmware transcript, then performs its independent
one-shot query. Each native XNU SMC records its CPU context with preemption
disabled. Firmware results do not change XNU's PAS admission cache.

The fixed delay does not prove the host logger is connected. Missing initial
`scheduled` UDP data leaves timing unverified; complete later call/return
records may still establish their query outcomes. Missing CPU/call/return/end
records cannot pass paired evidence checks.

`analyze-scm-pair-log.py` reconstructs and verifies the firmware checksum and
replays both query sequences in host RAM. Boolean false is kept distinct from
busy/error/not-reached. It reports query outcomes only. No GPU or PAS load/stop
is invoked by the pair boot path.

## Before hardware

The compiled kernel must match CI `42632917379` at `6ce4c7c`, the downloaded
SHA256SUMS and source pin. Verify all linked APIs, absence of unresolved
symbols, touch/PA64 markers and the actual preemption/CPU-capture/SMC callback.
Then run the local build command from [local-build.md](local-build.md) and
independently verify the produced boot against the accepted zapmem image.

Stop before device operations. The accepted device remains zapmem-b5006b5.
When testing is requested, use one temporary fastboot boot and bounded USB
capture, followed by one brief touch/wake confirmation. Avoid another control,
memory or full touch round. The result will distinguish whether false already
exists before XNU, changes across handoff, or remains unknown due to missing
logs. It will not establish GPU execution readiness.
