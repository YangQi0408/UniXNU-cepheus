# Native GPU Thermal Sampling

2026-11-10: private SWEET entrypoints now bind the existing GPU thermal sampler
to fixed TSENS mappings and the actual XNU monotonic time conversion. This is
an unhooked production building block, not a resource owner or device result.

## Native Contract

`cepheus_gpu_tsens_bind_native` copies the owner's validation callback and binds
once under a separate TSENS lock. Four fixed 4096-byte requests are mapped:
TM0 `0xc263000`, TM1 `0xc265000`, SROT0 `0xc222000`, SROT1 `0xc223000`.
Every mapping has an owner check before and after, including null returns.
The caller cannot provide an arbitrary address, mapper or clock callback.
The mapper uses the existing `ml_io_map` API with its default permissions;
there are no register stores, configuration/threshold/IRQ clears or recovery.
This is read-only behavior, not a claim of hardware read-only page permissions.

Logical 4 KiB windows are not necessarily the actual mapping footprint. XNU's
`osfmk/arm/io_map.c` truncates physical addresses and rounds allocation sizes
to host pages. In the SWEET 16 KiB configuration the owner must cover
`0xc260000..0xc264000`, `0xc264000..0xc268000` and
`0xc220000..0xc224000`; both SROT requests share that last physical page.
Distinct virtual logical windows do not establish independent physical owners.
The implementation retains permanent mappings for the boot lifetime; a failed
attempt does not unmap an uncertain alias or release the platform's lease.

`cepheus_gpu_temperature_sample_native` reads gpuss-0/TSENS0 sensor15 and
gpuss-1/TSENS1 sensor11. Each individual SROT enable, TM ready, critical/watchdog
status and temperature load has before/after owner checks and an ARM64 load
barrier. Invalid `UINT32_MAX` values still undergo the post-load check. Health
is checked again after both samples. No partially valid sample is returned.
Mapping alignment, size, fixed PA, overflow and overlapping logical VA windows
are rejected before dereferencing.

`cepheus_gpu_thermal_now_native` uses `mach_absolute_time` and
`absolutetime_to_nanoseconds`; owner checks enclose the time read. The shared
state rejects time regression across samples/calls. A sample also requires its
end time to advance and remain within 100 ms of its start. This does not prove
clock calibration, a hardware conversion count or a bound on kernel mapping
and bus transactions. A stopped converter can keep valid bits set; the actual
owner must independently prove conversion/clock health and exclude writers.

Any bind/sample/time failure after admission latches terminal refusal. Later
calls cannot rebind or invoke the invalidated owner again. Invalid context,
null inputs and concurrent/reentrant lock rejection do not claim a new resource
acquisition. Owner callbacks/context have a non-sleeping, boot-lifetime contract
and cannot recursively call the native TSENS APIs. No production owner exists
yet, and the GMU startup hook remains absent. Existing thermal stop handling
still distinguishes a refusal from proof that hardware actually stopped.

## Accepted Local Evidence

`out/tsens-native-local-20261110-final-v1/receipt.json` PASS:

- Generic sampler: 33 PROT_NONE owner boundaries, individual access/time guards,
  sensor/controller disable, watchdog/critical faults, late health changes,
  invalid bus values, all-invalid samples and stalled/backward/error time.
- Native adapter: 48 PROT_NONE boundaries across bind/sample/time, all four map
  failures plus coincident owner revocation, copied callback mutation, recursive
  rejection, repeated sampling, overlap and terminal no-retry behavior.
- Read-only mapped host windows reject accidental stores. Host, ASan/UBSan,
  native SWEET AArch64 compilation/assembly/analysis, host analysis and
  staged-only native compilation pass. Platform mapping/time/MMIO are modeled
  in host tests, not measurements of the phone.

`out/tsens-native-preflight-20261110-final-v1/receipt.json` passes all four local
kernel prerequisites, 71 host programs and seven production-unit host analyses.
SCM code generation, real-source configuration staging and no-startup-call
checks pass. The full preflight deliberately skips proprietary GMU/ZAP cases
without explicit inputs; it does not stand in for those cases.

A separate ASan/UBSan mixed C/C++ run with the real local GMU/SQE artifacts
passes all requested modes 0..22, actual HFI rings, thermal refusal and existing
failure/retirement paths in **emulated hardware**. It does not run the new native
TSENS backend on hardware. Log:
`out/tsens-native-local-20261110-final-v1/gmu-chain-asan.log`, SHA256
`aad50b02888db2098ac2c64e18a7beddff9b837a905d0241b380f4cb50239aba`.
Leak detection is disabled for deliberately retained quarantine backing; this
is not a leak-free claim. The optional real ZAP case was not supplied here.

Independent downstream macro/FDT cross-check passes with local Android commit
`36f369fdb77319a2984a48b89f025ac3e5f97386` and the captured merged FDT. The
fixture clock now advances instead of returning the same timestamp twice.
Original reference sources are unmodified. Input SHA256:

```text
drivers/thermal/tsens2xxx.c
62a3f8b201783a9e289c44c5ce3007cd79fa248d9301308197293369319bfccc
drivers/thermal/tsens.h
bd38f78a3f2719a7dc4337a411b2ee805db63c6309475e8e1ba20d46754924d9
drivers/thermal/msm-tsens.c
dc623ca78b8b97adabd62253f29278dfc19bceea0409175677ab47f68b2fe3f6
live_merged.dtb
bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274
```

The initial `out/tsens-native-local-20261110-v1` failed an assertion: owner
revocation during the nested clock read was followed by another callback.
Immediate native failure latching fixes that path. Preserve the failed receipt;
v2 predates additional health/time cases and is not the final source identity.
Final receipts bind dirty source hashes at parent `c363e2e`, not a later commit.

## Remaining Work

Full XNU compilation/link and artifact acceptance are still required for this
batch and the preceding ARC/native-power changes before integrated use. No
standalone CI, phone test or image repackaging was performed for this module.
The accepted exit-tail diagnostic remains exactly `42835691416 @2bf48ab` and
boot SHA256 `b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`.
It has not been relabeled as containing this backend.

The next integrated work is real resource ownership, inherited requests and
idle/foreign-writer coordination, plus native platform bindings for subsequent
GMU/SMMU stages. TSENS conversion health, independent supervision, PAS13
supported=false, secure PA/crypto and SMMU admission remain open. CP halt is
preserved; GPU scratch/fence, PAS authentication and Metal have not executed.
