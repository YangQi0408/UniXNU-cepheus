# Native GMU Upload

2026-11-11: `cepheus_gmu_upload_native` binds fixed SWEET mappings, the
actual six-buffer pool and a sealed A640 firmware snapshot to the existing
copy/readback engine. It is an unhooked production building block, not a real
resource owner, running firmware, PAS authentication or GPU execution.

## Contract

Boot-lifetime one-shot in kernel thread context, serialized with
GPLL0/counter/power/CM3/PDC/SMMU. Owner callbacks and publication are copied.
No recursive native calls; callbacks are non-sleeping. A real owner must protect
the installed SID5/context/root/TBU, actual clocks/rails, thermal supervision,
snapshot/workspace identity and all relevant writers and idle transitions.
There is no production owner yet. Acquisition cannot hide prerequisite stores
or upload, and failed acquisition must unwind without device effects.

Fixed maps: GCC `0x100000/0x90000`, GPUCC `0x2c90000/0x9000`, GMU
`0x2c6a000/0x30000`. All map returns, including null, have pre/post validation.
16 KiB GMU/GPUCC footprints overlap as described for CM3/PDC. Maps are permanent
and do not contain CPU bus faults or grant hardware access. Every GMU access
checks actual GCC/GPUCC prerequisites and active MEMNOC/DVM branches. Existing
TCM retention and reset=1 are required, never established by hidden stores.

The real pool returns exact ICACHE/DCACHE mappings from the published group,
validates materialized PTEs/fresh HFI and derives configuration from actual
backing. Its temporary upload pin excludes installation, retirement and
abandonment. It does not exclude foreign CPU/device writers. Native upload
accepts only a sealed firmware handle through the real snapshot verifier,
not caller-supplied file/target pointers. No proprietary bytes are committed.

Shared copy/readback now validates before and after every TCM word and cache
byte access, orders each destination access, and counts an attempted store
before performing it. The MMIO reset/fault adapter latches terminal access
failure. Actual Normal-NC pool publication validates PTE/HFI backing and orders
it. Final success rechecks reset, retention, GMU fault and pool/access health.
No reset release, SMMU mutation, GX access, PAS call or CP submission occurs.

Any attempted destination store retains hardware prerequisites, even on
success. Failure after stores quarantines all six backing allocations and
terminates the pool without freeing them. Failed pin cleanup reports
quarantine and retains hardware/pin even before stores. Successful completion
ends this temporary pool pin only; a real retained hardware owner is still
required. No uncertain cleanup, automatic recovery or retry is implemented.

## Local Evidence

Accepted `out/upload-native-local-20261111-final-v1/receipt.json`: PASS,
SHA256 `c5528ec22551a5743d17a4c44ceaf7026a6ab5be36cfe03e6c061854b19c2441`.
8433 individually revoked native PROT_NONE boundaries, three map failures and
coincident revocation, copied owner/publication mutation, recursive rejection,
reset/retention/off-clock refusal, pool publish/finish failures and terminal
retention pass. Shared uploader covers 51 PROT_NONE TCM/cache access boundaries.
Host/ASan/UBSan, actual native/staged AArch64 compile/assembly and native/host
static analyses pass. These map register and completion observations are models.

Explicit real GMU/SQE-input ASan/UBSan run exits 0:
`out/upload-native-local-20261111-final-v1/gmu-chain-asan.log`, SHA256
`69a32365cf0a473c9d2307c91037ce277b62bca50e18004de687c7d5a4206d5a`.
It includes actual upload pool modes 0..7 (including concurrent retirement/
abandonment rejection), native+sealed-file+actual-pool modes 0..5, existing
SMMU pool integration and whole-chain modes 0..22. Registers/IOKit/ACKs are
emulated; real firmware file input is not phone execution. Optional ZAP was
not supplied. Leak detection is disabled for deliberate quarantine retention,
not a leak-free claim. Default preflight skips real image cases without inputs.

Sources at dirty parent `b5c8f2a`: native-private SHA256
`e9438c19bded6fad6007309885c1a1768257444aa4b93a1c8d4f876aa73e15c7`,
memory.cpp SHA256
`c41bca9f06d3c70eda5bc6e4b51600bd0b4c1d1af7d803a7e8254a140b429c43`.
Earlier v1 lacked new staging inputs; v2 exposed a hard-coded test check index,
and preflight v1 exposed GCC misleading-indentation in a test. Failures remain
archived, warnings are not suppressed and those runs are not accepted final.

## Next Integration

`out/upload-native-preflight-20261111-final-v1/receipt.json` passes all four
local prerequisites, 75 host programs and seven production-unit analyses.
No-startup and staging checks pass. This is not complete kernel acceptance.
Preflight receipt SHA256:
`88d4ee34d13bcfbb1aa5566f7a4de0bac16e7889cfe74231052a9c39d27d13c1`.

Committed as `d22a054` after SMMU `b5c8f2a`; combined full XNU run
`42860421403 @d22a0541c5f4afd17fcf5cef90625dc76801405f` started at
2026-11-11 01:00:29 Asia/Shanghai and completed successfully at about 01:33.
The downloaded 17472504-byte kernel has SHA256
`801079f0e46560de5ee4c2cd0ff5d36bb5b56402012cf42f078fec610c457656`.
`out/kernel-verification-ci-42860421403.json` independently accepts 94 core/
30 memory entries, no unresolved symbols, exact archived d22a054 source,
touch/PA64/native SCM and tail ABI/helper/caller. Receipt SHA256:
`1fd028a926a09276c9a6012783e3f14b09a4794697576ea27374df2ac1aea9c6`.
The verifier ran from a `git archive d22a054` tree because newer idle source
is not part of that build; no current-HEAD relabeling. No GPU execution.

Full combined native compilation/link and independent artifact acceptance
are complete. Continue the real owner/
inherited-resource/idle work and integrated startup. No phone operation or new
boot image was produced by this module.

Accepted unbooted exit-tail diagnostic stays CI `42835691416 @2bf48ab`, boot
SHA256 `b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`.
It excludes these native changes and is not relabeled HEAD. PAS13=false,
supported=false, secure PA/crypto and CP halt remain independent gates.
GPU scratch/fence and Metal have not executed; the goal remains active.
