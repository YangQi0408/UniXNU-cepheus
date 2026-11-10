# Resource Owner Boundary

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

## Required Next Evidence

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
