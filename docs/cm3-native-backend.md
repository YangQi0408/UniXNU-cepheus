# Native CM3 Cold Start

## Config-Required Follow-On

The private cold-start signature now takes owner, GMU start configuration,
native GPU configuration and output. Exact retained DB/fuse config, typed
bin policy, chipid, initial frequency1/BW3 are required before admission.
Bin1 requires actual native QMP consumption with a retained RPMh token before
CM3; token/config lifetime is checked around every owner validation. Successful
final owner callback is followed by a readiness check; loss changes the actual
CM3 transaction to QUARANTINED before returning an error.
Focused CM3 fixtures use labeled synthetic bin0 identity, not an actual DB/fuse.
3424 immediate/5464 delayed owner PROT_NONE boundaries and exhaustive config
loss cases pass. The actual C/C++ pool fixture instead builds real native
configuration before CM3 and supplies native QMP first for bin1. Acceptance
and explicit hardware/global-owner limitations are recorded in
`hfi-native-backend.md` under Config And Pre-Cold ACD Follow-On.
Older evidence below does not cover this API/lifetime change.

2026-11-11: `cepheus_gmu_start_cold_native` binds the existing A640 cold-start
engine to fixed SWEET mappings, real kernel time and `delay(1)`. It has no
startup caller and implements no resource owner. CM3/HFI readiness is not GPU
execution, PAS authentication or permission to unhalt CP.

## Contract

The boot-lifetime singleton copies the owner callbacks and startup configuration.
It shares the GPLL0/counter/native-power lock. Invalid context, inputs and lock
contention do not acquire resources; a second admitted attempt is refused
without changing output or calling the owner again. All callbacks are
non-sleeping, boot-lifetime operations under that lock. They cannot recursively
enter the serialized native APIs.

The owner must acquire and continuously protect actual cold power, PDC/RSC,
SMMU mappings, firmware/workspace publication and thermal supervision.
Preparation does not upload firmware or establish these prerequisites through
hidden writes. A failed owner acquisition must unwind its own partial
acquisition. There is no production implementation of this owner yet.

Mapping requests are GCC `0x100000/0x90000`, GPUCC `0x2c90000/0x9000` and
GMU `0x2c6a000/0x30000`. A live owner check encloses each mapping, including
null returns. Under SWEET's 16 KiB host pages the latter two footprints are
`0x2c90000..0x2c9c000` and `0x2c68000..0x2c9c000`. They overlap physically;
the owner must cover these aliases, not treat them as independent resources.
Native mappings are permanent. The mapper uses existing default permissions,
not hardware read-only permissions or a CPU-bus-fault containment mechanism.

Before each GMU access, actual GCC/GPUCC status must admit the CFG branch, CX
and GMU access clocks. Individual prerequisite reads also have owner checks
before and after. The native failure latch stops callbacks after access or
ownership loss. Existing TCM retention must equal 1; this stage never silently
sets it. Existing reset, HFI and fault checks retain the cold-only admission.

The real register sequence has 18 attempted effects, including configuration,
reset release and HFI initialization. Shared GMU MMIO now validates after each
load/store, rejects `UINT32_MAX` after the post-load check, preserves output on
failed reads and rejects pointer-range overflow. These shared changes also
affect OOB/HFI/upload/retirement users, not only native CM3 startup.

Native time uses `mach_absolute_time` and `absolutetime_to_nanoseconds`, guarded
on both sides and with regression rejection. Each pause invokes `delay(1)` and
requires measured elapsed time in 1..100 us. Polls retain the existing 100 ms
and 10000-iteration limits per wait. This does not bound kernel mapping/MMIO
latency or establish physical clock calibration. Frozen/reversed/late pauses
are refused instead of being treated as successful hardware waits.

Successful acquisition followed by mapping or pre-store failure calls
`finish(keep=0)` once. Every attempted store, including uncertain reset release,
retains all prerequisites with `keep=1`. Success also retains them. There is no
reset retry, recovery, unmap, resource-lowering or CP operation.

## Local Evidence

`out/cm3-native-local-20261111-final-v1/receipt.json` PASS:

- 3423 immediate-ACK and 5463 delayed-ACK PROT_NONE owner boundaries. Every
  boundary is separately revoked; failed attempts stop callbacks and retries.
- Fixed map arguments and each map failure, copied callbacks/configuration,
  recursive lock rejection, cold retention/access gates and uncertain stores.
- Separate 3/5-pause CM3/HFI success; 9999-pause success and 10000-pause refusal
  for both waits; timeout, stopped/reversed/late time, firmware faults and bad
  HFI version. A host RAM model supplies ACKs, not the phone.
- Host and ASan/UBSan execution, actual SWEET native AArch64 compile/assembly,
  host/native static analysis and staged-only native compilation pass.

`out/cm3-native-preflight-20261111-final-v1/receipt.json` passes all four local
prerequisites, 72 host programs and seven production-unit analyses. Staging,
SCM code generation and no-startup-call checks pass. Both receipts bind source
hashes at dirty parent `75590d8`, not a later commit or complete XNU build.

The real GMU/SQE-input mixed C/C++ ASan/UBSan regression exits 0, including
whole-chain modes 0..22. Hardware remains emulated; this is not execution of
the native CM3 backend on the phone. Log:
`out/cm3-native-local-20261111-final-v1/gmu-chain-asan.log`, SHA256
`da5a1e04572e1a05b1745103eba916ace08620d9aa810acd8f5d9b7f4cb42d3e`.
Leak detection is disabled for intentionally retained quarantine backing, not
a leak-free claim. The optional real ZAP artifact was not supplied.

## Remaining Integration

Full XNU compilation/link and artifact acceptance remain necessary for this
batch and the preceding ARC/native-power/TSENS changes. No separate CI, image
repackaging or phone operation was performed. The accepted unbooted exit-tail
diagnostic remains exactly CI `42835691416 @2bf48ab`, boot SHA256
`b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`.
It does not contain these later backends and has not been relabeled as HEAD.

Next: native PDC/RSC and SMMU/image-publication bindings, with real resource
ownership, inherited-vote provenance and idle/foreign-writer coordination.
PAS13 supported=false, secure PA/crypto and CP halt remain independent gates.
GPU scratch/fence, authentication and Metal have not executed.
