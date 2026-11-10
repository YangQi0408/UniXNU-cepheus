# UEFI/XNU PAS13 paired diagnostic

## Hardware Result: 2026-11-10

One temporary boot of the exact candidate below succeeded on `67eea163/cepheus`
at 12:27:29 +08:00. No partition was flashed. The saved production-policy
analysis returns `PAIR_COMPLETE`: seven UEFI calls and seven XNU calls,
including CPU context, complete input/output and pair end; the reconstructed
2656-byte handoff checksum passes. Both stages report availability `0x1f`.
Both PAS13 inputs are `0x42000207:0x1:0xd:0:0:0:0:0`, returning eight zeros:
**false before ExitBootServices and false in XNU**. The policy `e=-1` records
unsupported status here, not a PAS authentication attempt or transport error.

CurrentEL is `0x4` in both stages. UEFI MPIDR is `0x81000000`; all XNU calls
record `0x81000200`. Different CPU records do not establish a causal explanation.
This pair excludes a change occurring only after XNU handoff as a necessary
explanation of this boot's false. It does not identify the running secure image,
exclude EL2 mediation, or explain runtime descriptors/cache or boot history.
Do not repeat this query or bypass supported=false on the basis of this result.

The initial scheduled marker was received; the paired end was received at
12:29:42.469353 +08:00. UDP receipt times do not verify the kernel's 90-second
wait, so `logger_delay_verified=false` is retained. The user replied `正常`
after the requested brief tap/swipe and power sleep/wake check, observed by
12:30:55 while capture was still active. Five post-query health records show
events 3 to 189, HID calls to 184 and zero errors; blank/display-on records
also exist. This user report is not a long-term stability claim.

Capture was explicitly ended at 12:30:56, 245.090645 seconds of a 300-second
maximum: 396 accepted packets, 143442 bytes, zero rejected packets. Packet
accounting and log SHA256 pass; host RX errors/drops were zero. Thirty complete
RAW batches (124 of 128 bounded events) replay with no reporting differences;
the final partial batch is omitted, not fabricated. Capture and the owned
watcher have exited; no automatic reboot, restore or retry occurred. The phone
is left running this accepted paired candidate, superseding the earlier
zapmem-b5006b5 running-state record below.

Evidence: `../logs/scmpair-6ce4c7c-live-20261110-1226/` contains `result.json`,
`scm-pair.json`, raw `boot.log`, `packets.jsonl`, `capture.json`, `fastboot.log`,
`touch-replay.json` and final host USB/link observations. Raw log SHA256:
`d064993a178115a05e66c34136419c842e3578bb0a633a0e46facc1fa35c64fb`.
GPU/PAS authentication and Metal were not executed. Next work is offline
investigation of the pre-XNU secure/boot environment plus the independent
resource/crypto/secure-memory/power/SMMU backend gaps, not another control round.

## Verified Candidate

On 2026-11-09 the kernel pair implementation was committed as `e5e7104`.
CI `42627950109` failed before kernel compilation because the GX host fixture
included the newly inserted pair helper. Commit `6ce4c7c` fixes that extraction;
CI `42632917379` completed successfully at 22:43:36 +08:00, including the full
XNU build, staged-source comparisons and both artifact uploads. Its exact
kernel was downloaded and independently verified locally: 17449368 bytes,
SHA256 `a2a4837d80a17dfcc2dc8fa5847d03308079cc0c691f7045dfba1e86a7b66c2e`.
The verification covers 81 core entrypoints, 22 memory APIs, no unresolved
symbols, production source identity, ordered touch, PA64 cache callback and
the compiled preemption/CPU-capture/SMC callback. Evidence:
`out/kernel-verification-ci-42632917379.json` and
`../logs/scm-pair-ci-42632917379/build.log`.

The real candidate was built locally with `build-scm-pair-local.py`, using
packaging container `cepheus-build:latest` with networking disabled:
`out/scm-pair-final-6ce4c7c/boot-scm-pair.img`, 70688768 bytes,
SHA256 `2c93dc3333665bdbc8f1c835c0e59226f5b2a0cfda2678d4bb121a3fc86903d2`.
All four build/repack/package/independent-verification steps passed. Receipts:
`out/scm-pair-final-6ce4c7c/receipt.json` and `boot-verification.json`.
At the end of this 2026-11-09 preparation the image had not been booted.
The subsequent single hardware result is recorded above.

Local DarwinBoot build:
`out/scm-pair-final-6ce4c7c/firmware/build/DarwinBoot/RELEASE_CLANGPDB/AARCH64/DarwinBoot.efi`,
SHA256 `8ce496a6621101c3354dc96cf031fb7ac1dc2ca89775cccddf19a07c41f4051b`.
Local FV replacement: `out/scm-pair-final-6ce4c7c/fv/scm-pair.fd`, 3145728 bytes,
SHA256 `7fd09e2c96b0f53cb8e608ed82bbd11ca694547f7c5da2d53c8ca928575d20f6`.
The FV receipt proves unchanged baseline reconstruction, 197 nested files,
only DarwinBoot changed internally and only its compressed wrapper changed
in the outer FV. The actual embedded EFI matches the compiled file.
Actual EFI disassembly has one SMC site at RVA `0x3568`, preceded by consecutive
CurrentEL, MPIDR_EL1 and CNTPCT_EL0 reads at `0x355c`, `0x3560` and `0x3564`.
This is compiled-artifact evidence, not firmware execution evidence.

The complete kernel prerequisite run passed locally with an explicit XNU
reference: `out/local-kernel-preflight-v4/receipt.json` (65 host programs and
seven production-unit static analyses). The actual staged ADT
parser, handoff policy/corruption checks, firmware codegen, nine paired-log
tests and end-to-end packaging fixture with ten rejection cases also pass.
The packaging fixture uses a synthetic kernel and is temporary; it is not a
bootable candidate. It is separate from the verified real image above.

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

## Next Hardware Point

The compiled kernel matches CI `42632917379` at `6ce4c7c`, SHA256SUMS and the
pinned ios-sweet source. Independent boot verification proves the accepted
zapmem shim/DTB and unrelated FAT files are unchanged, the actual kernel and
EFI match their verified inputs, and the only profile delta is replacing
`cephzapmem=1` with `cephscmpair=1` plus a zero 2656-byte ADT slot.

The following was the 2026-11-09 test plan, now executed once as recorded above;
it is not a request to repeat the test. At that time the device ran zapmem-b5006b5.
When testing is requested, use one temporary fastboot boot and bounded USB
capture, followed by one brief touch/wake confirmation. Avoid another control,
memory or full touch round. The result will distinguish whether false already
exists before XNU, changes across handoff, or remains unknown due to missing
logs. It will not establish GPU execution readiness.

Analyze the bounded saved capture, preserving its original bytes:

```sh
python3 scripts/analyze-scm-pair-log.py CAPTURE.log --output NEW_RESULT.json
```

PAS13=false remains unresolved. Real resource acquire/lease/quiesce,
crypto bandwidth, secure PA/XPU and shared power/SMMU ownership still need
implementation; GPU scratch/fence and Metal have not executed.
