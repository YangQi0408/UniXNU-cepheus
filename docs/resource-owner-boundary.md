# Resource Owner Boundary

2026-11-11 owner-evidence admission: `cepheus_owner_evidence.h` now defines a
pure, fail-closed gate over immutable copied records. It requires both TBUs at
their exact MMIO bases and `[0,0x400)`/`[0x400,0x800)` SID ranges, the two
local GX-user SID bits, a sender-owned writer/lease identity, complete RPMh
ledger provenance, and a matching ACK epoch/count. ECATS debug status,
uncertain/simulated/stale/foreign state and partial or stale records cannot
admit. Inputs are copied, outputs may not alias any record, and an uncertain
attempt permanently retains the gate. The linked wrapper has no startup
caller. This validates a record contract only; it does not acquire either TBU,
observe a real owner, consume an inherited request or authorize a GPU store.

2026-11-10 source review after native power integration. This identifies the
next owner work; it does not implement an owner, observe hardware sleep state
or authorize an ARC/BCM/GCC store.

## Three Separate Boundaries

1. XNU software serialization. Touch VRM and GPU ARC/BCM share `rh_lock` and
   the terminal transport poison. The lock excludes participating software
   callers only. GPLL0/counter/native power have a different shared lock;
   the future owner must define acquisition order, retained state and callback
   rules across both, not assume they are one lock.
2. CPU idle. The reference `osfmk/arm64/cpu.c` declares the `wfi` tunable with
   default 1. `cpu_machine_idle_init` sets `idle_enable=TRUE` without a nonzero
   `jtag` override, and `cpu_idle` has a `if (wfi)` path to WFI/retention WFI.
   `cpu_idle_notify`, when present, can execute on entry/exit. The SWEET board
   sets `NO_MONITOR=1`; that removes the monitor setup, not the WFI path.
   The accepted tail profile has `cpus=8` and no explicit `wfi=0`/`jtag`
   override. These are source/profile facts, not observed runtime tunables.
3. Inherited hardware/foreign agents. RSC solver, armed sleep/wake commands,
   other DRVs, secure/EL2 execution and any runtime/DMA writer are not excluded
   by the software lock or a local DAIF mask. An idle TCS is not an empty
   request ledger; normal-bank scanning is not a global writer proof.

The SWEET CPU source's `quiesceCPU` and `haltCPU` panic because those platform
operations are unimplemented. This excludes successful execution of those
specific suspend methods, not ordinary idle or firmware-programmed triggers.
Screen blank/unblank success therefore does not prove a system suspend cycle.
Neither an absent CPU_SUSPEND implementation nor `NO_MONITOR` establishes a
safe resource-owner lease. No boot arguments or CPU idle code were changed.

## Idle Callback And Touch Writer

2026-11-11 continuation while combined native XNU CI `42860421403 @d22a054`
is pending. Actual SWEET CPU registration zero-initializes
`ml_processor_info_t` and does not assign `processor_idle` or
`platform_cache_dispatch`. The inspected `ml_processor_register` copies
`processor_idle` directly into each CPU's `cpu_idle_notify`.
`processor_idle_t` returns void and receives only CPU id, entry/exit and a
timeout output. `cpu_idle` calls it before WFI but does not test a veto result;
the timeout controls the idle timer, not permission to enter WFI. Adding a
successful notification callback would not establish an idle hold.

The old pinned RPMh source's opening comment asserts "deep idle disabled";
neither its touch-power function nor shared `rh_lock` implements that policy.
The applied Cepheus power transform adds `cepheus_touch_power_enable`, taking
the same `rh_lock` and sending only `ldoc1` XOB enable=1. It does not add idle
coordination. GPU ARC/BCM uses this same lock/terminal poison, while native
GPLL0/power/CM3/PDC/SMMU/upload uses the separate GPLL0 lock. The GPU owner must
coordinate both participating writer domains and inherited hardware state.
No idle toggle, callback or fake owner was added in this inspection.

These facts come from the read-only older XNU reference and pinned patches,
not a runtime policy measurement or complete current linked-tree proof:

```text
osfmk/arm64/machine_routines.c
4914ed3dd20c16da933b374980974eadbf17023bd5e1fe8251fdec5723810d4b
osfmk/arm/machine_routines.h
1eec9e9dac950421a27a6db83e81adc05ea15eac6549542805ec7a764c43644e
```

Actual pinned `0062-sweet-touch-power-battery.patch` SHA256 is
`8a4dc08c61545d31bd2c0a70530434824b3e3d6c4bf96e8b63559db31148a617`.
Local `scripts/prepare-touch-power-patch.py` SHA256 is
`85ef5cac48574d1e5816fc674c93aa48f5ea1523d53b8f77faefa50d5b232a48`;
accepted generated `0084-cepheus-touch-power.patch` SHA256 is
`2d70d35fadc2beb7eed2c90c9f2e400986f379a4c52a6a0d5c6e4b4e66865868`.
No sibling input was edited. Still verify the complete applied build and
hardware behavior before any production owner/startup admission.

## Required Next Evidence

### Masked TBU Coverage

`scripts/gpu-fdt-config.py` now resolves every SID matched by each GMU/GX
mask to active, nonoverlapping TBU ranges. Missing high-SID coverage, disabled
ancestors, translated/unspecified bus ranges and overlapping MMIO are rejected.
It does not access TBU registers or authorize any SMMU store.

`out/smmu-tbu-source-20261111-v1/receipt.json` passes a separate pinned-vendor
source audit. Vendor commit is `36f369fdb77319a2984a48b89f025ac3e5f97386`;
all included DTS/header/driver files are byte-compared to that commit. Real
vendor DTS files are compiled by CPP/dtc using clearly labeled synthetic
external phandle stubs, NOT a board/live-FDT capture. Compiler warnings about
vendor unit-address syntax and incomplete interrupt stubs are preserved.

Masked matches are GMU kernel `[0x005,0x405]`, GMU user `[0x004,0x404]`,
GX user `[0x000,0x001,0x400,0x401]`, GX secure `[0x002,0x402]`. Every stream
needs both TBU0 (`0x2cc5000/0x1000`, SID `[0,0x400)`) and TBU1
(`0x2cc9000/0x1000`, SID `[0x400,0x800)`). Their status windows are
`0x2cc2200/8` and `0x2cc2208/8`. The parent requires CX and MEMNOC/DVM/AHB
clocks and carries `qcom,skip-init`/three-level tables.

Follow-on pinned `drivers/iommu/arm-smmu.c:5597` inspection establishes that
these status windows are ECATS DEBUG hardware-lock registers, not power-ready
or global-owner evidence. `qsmmuv500_ecats_lock` skips the inaccessible status
register on version1.0, otherwise polls for value1; `ecats_unlock` writes0.
Do not introduce a passive readiness sampler or ownership claim from these
values. No new TBU access was added.

The source audit also verifies `qsmmuv500_tbu_halt` can clear faults, alter
SCTLR, issue RESUME_TERMINATE and undo a halt on timeout. This destructive
debug path must not be used as passive ownership acquisition. TBU list
registration, an empty TCS and the new CPU-idle token still do not establish
inherited traffic/writer exclusion.

Six FDT test cases pass (one recorded-live-FDT test explicitly skipped because
its file is unavailable). `/tmp/live-cepheus.dtb` is not present; no synthetic
file is substituted for the historical recorded SHA. Reacquire an actual
capture for runtime topology acceptance. This work changes only offline tools
and tests; the in-flight `91a5ec1` production source remains unchanged.

2026-11-11 follow-on implementation: `gpu-idle-coordination.md` now describes
actual scoped SWEET XNU `cpu_idle`/preceding-WFE reader/block hooks and a
one-shot drain/retain API. It has no startup caller or runtime acceptance.
This replaces only the missing local idle-path coordination component;
foreign/hardware writers and resource ownership remain separate, unsolved.

The actual owner must identify every participating XNU writer, own the exact
mapped resources and retained inherited request state, and prevent or coordinate
resource-affecting idle transitions throughout use, including preemption onto
other cores. A local interrupt mask, active caller thread or callback returning
zero is insufficient. Any coordination API must also work with retained
uncertainty and thermal supervision; it cannot simply release on timeout.

For inherited RSC state, first obtain the already prepared exit-tail/read-only
GPU diagnostic on the phone. Its software requests can reduce uncertainty but
cannot be promoted to a completed hardware vote or exclusive owner. Bind any
eventual active request ledger to real command-db identity and acknowledged
transactions; do not initialize current CX/MX to zero from empty TCS contents.
The diagnostic's missing BCM/clock/global-writer coverage remains explicit.

Do not disable global idle or change inherited sleep/wake programming merely
to satisfy a boolean gate. A scoped, proven platform policy and the associated
resource lifetime are needed before adding a startup caller. Authentication,
secure PA/crypto, SMMU and CP halt remain separate conditions.

2026-11-11 follow-on: `gpll0-scope-coordination.md` describes actual parent
token checks and permanent RPMh/idle retention before a parent vote store or
zero-store successful adoption. Local focused acceptance passes; complete
link/runtime remain separate. This adds cross-domain lifecycle coordination,
not a complete source writer census or a global hardware writer lease.

Native HFI continuation checks native configuration under `rh_lock` while it
holds GPLL0 serialization. All copied config/HFI owner callbacks must remain
non-sleeping, non-reentrant and must not acquire GPLL0 while holding `rh_lock`.
This explicit local lock order does not prove exclusion of firmware, other
DRVs or DMA, or create a real global owner. Also require AOP ACD consumption
before CM3 release for bin1; a QMP success recorded only later is insufficient.

## Inspected Inputs

The existing `../kernel-test/xnu` tree is a read-only, older source reference,
not the accepted CI build tree. SHA256 at review:

```text
osfmk/arm64/cpu.c
dadb0aac8aeed046d719dc51b1723066ca5c29d065a66b79cc70a261e06b76d7
osfmk/arm/caches.c
1f60ebd42161f2e4d064100f008e84a7f2133272601602cd5c67fe1be2c2a41f
iokit/Kernel/arm/SweetCPU.cpp
73e4e1e0dfb0af5b82581d44d34ec0130b84b7bbcc86198a5e63540df148086d
```

Pinned ios-sweet `45263bee51df4a0f55cdf9c649af886399488ddb` CPU patches
independently retain the rejection methods and add PSCI CPU_ON, not CPU_SUSPEND:

```text
0018-sweet-boot-cpu-driver.patch
06de6e84d8ff9b0307872fb78a827e84fc1e16a1b7286a87f0e618a5dc2692a9
0065-sweet-smp-psci.patch
4aad63a0c924ca516f7960c0484b1a4bea6c97ae4581e20fef2cea0cfe489d16
```

Future integrated acceptance must verify the actually applied complete build
tree and runtime policy. These hashes record this source inspection, not a
hardware/lease receipt or complete firmware writer census.
