# Native GPU PDC/RSC

2026-11-11: `cepheus_gpu_pdc_install_native` binds the existing A640v2
48-step PDC/RSC installer to actual SWEET `ml_io_map` and guarded volatile
access. It is an unhooked production building block, not a real resource owner,
an apps-DRV vote completion or a GPU execution result.

## Contract

The boot-lifetime singleton copies its owner callbacks, command-db resource
input and GMU configuration, then uses the GPLL0/counter/power/CM3 lock.
Callbacks are non-sleeping and cannot recursively enter serialized native
APIs. Invalid context/input/lock rejection does not acquire resources; admitted
attempts are terminal, with no retry or output mutation on subsequent calls.

The owner must acquire and protect actual cold PDC/RSC, CX/access clocks,
firmware/workspace identity, command-db addresses/levels and thermal supervision
across all relevant writers and idle transitions. There is no production owner
implementation. Acquisition must not hide prerequisite stores or firmware
upload. Failure must unwind partial owner acquisition without device effects.

Fixed mapping requests:

```text
GCC       0x0100000 / 0x90000
GPUCC     0x2c90000 / 0x09000
GMU/RSC   0x2c6a000 / 0x30000
PDC CFG   0xb290000 / 0x10000
PDC SEQ   0xb490000 / 0x10000
```

Each map has a live owner check before and after, including null returns.
GCC/CFG/SEQ are 16 KiB aligned; GPUCC and GMU mapping footprints are
`0x2c90000..0x2c9c000` and `0x2c68000..0x2c9c000` respectively and overlap
physically. Owners must cover both aliases. Mappings use existing default
permissions and remain permanent; mapping is not CPU bus-fault containment,
a power grant or read-only page protection.

Every GMU prerequisite and PDC/RSC access is checked on both sides. Actual
GCC/GPUCC status gates GMU access. Existing TCM retention must equal 1;
CM3 must remain reset and GX/SPTP off. Neither retention nor these prerequisites
is established through hidden writes in `begin`. The installer refuses inherited
PDC or command-bank enable bits without disabling or overwriting them.

The shared PDC adapter removes bare cold-state reads, rejects pointer overflow
and invalid bus values, preserves output after failed reads and checks ownership
after stores/barriers. It latches access failure so later calls cannot revisit
revoked mappings. It rechecks the PDC lease after nested GMU checks even when
the two windows have distinct owner callbacks. Protocol readback failure remains
terminal in the installer. Input tables still require exact ePCB addresses
`0x30010/0x30000/0x30090`; metadata is not fabricated from empty TCS contents.

Existing sequencing preserves 48 ordered attempted effects and late bank/PDC
enables. RSC_STATUS0 is mixed control/status, so its first write does not use
whole-register equality readback. The sequence never touches apps-DRV banks,
releases CM3 reset, installs SMMU, authenticates PAS or unhalts CP.

After acquisition, mapping/pre-store failures finish once with `keep=0`.
Every attempted store, including uncertain PDC enable, retains all prerequisites
with `keep=1`. Success retains them too. No recovery/reset/unmap or automatic
resource lowering is implemented.

## Local Evidence

`out/pdc-native-local-20261111-final-v1/receipt.json` PASS:

- Native adapter: 34844 individually revoked PROT_NONE owner boundaries,
  five fixed maps and failures, copied callback/config/resource mutations,
  recursive lock rejection, cold retention/reset/GX/PDC enable refusal,
  off-clock protected-aperture rejection and terminal retention/no retry.
- Shared MMIO adapter: 2807 PROT_NONE boundaries, all 48 ordered writes,
  guarded cold reads/stores, invalid-value output atomicity, pointer overflow,
  sticky failure and distinct PDC/GMU callback revocation before any access.
- Host/ASan/UBSan, actual native SWEET AArch64 compile/assembly/static analysis,
  host analysis and staged-only compilation pass. MMIO and completion are host
  RAM models, not observations of the phone.

`out/pdc-native-preflight-20261111-final-v1/receipt.json` passes all four local
prerequisites, 73 host programs and seven production-unit analyses. Staging,
SCM code generation and no-startup-call checks pass. Receipts bind dirty source
hashes at parent `ce190d9`; they are not complete XNU link/artifact acceptance.

Real GMU/SQE-input mixed C/C++ ASan/UBSan chain modes 0..22 exit 0 with emulated
hardware. Log `out/pdc-native-local-20261111-final-v1/gmu-chain-asan.log`, SHA256
`530b021bd77b2532bad0fd7b51e9a29a3378b05cfd019722ff673c9678061bee`.
Leak detection is disabled for intentional quarantine retention, not a leak-free
claim. Optional real ZAP inputs were not supplied. The full local preflight also
deliberately skips proprietary-input cases without explicit input paths.

Explicit captured-FDT tests pass and confirm CFG/SEQ addresses, GMU stream
and associated platform configuration. Log `live-fdt.log` in that directory,
SHA256 `7bcf747b796eaed17882843dfcfb6935a4c5ac6154a24821784099c0d1f27d08`.
Captured FDT SHA256 remains
`bdd364a16ceabe9960e128103162e5280b150303624a123dfd71e871f9a43274`.
Local read-only Android `36f369fdb77319a2984a48b89f025ac3e5f97386` source
inspection corroborates RSC/PDC register locations, program words and strides:

```text
drivers/gpu/msm/adreno_a6xx_gmu.c
8919acc7bddc232fd1e262bad4a72acc18f6c9e7ef8282c7399c782134e27170
drivers/gpu/msm/a6xx_reg.h
1b47f5ab09658398e2f20a36e00b4380987a29956c4e6be4ec3036ee04df5a69
```

This inspection and the captured FDT do not establish runtime ownership, actual
command-db identity or resource completion. No sibling input source was edited.
The initial passing `pdc-native-local-20261111-v1` predates the final distinct-
callback postcheck and is not the accepted final source identity.

## Next Integration

Combined native batches still require full XNU compilation/link and independent
artifact acceptance before integrated use. No separate CI/phone/image operation
was performed for this module. Accepted unbooted exit-tail diagnostic stays
CI `42835691416 @2bf48ab` with boot SHA256
`b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`;
it does not contain these later native backends and is not relabeled as HEAD.

Continue real inherited-resource ownership, idle/foreign-writer coordination
and native SMMU/image-publication bindings. PAS13 supported=false, secure PA/
crypto and CP halt remain independent gates. GPU scratch/fence, authentication
and Metal have not executed; the overall goal remains active and incomplete.
