# Native CX/GMU Power Transport

2026-11-10: `cepheus_gpu_power_start_native` binds the existing cold power
transaction to actual SWEET `ml_io_map`, volatile register access and `delay`.
It is an unhooked private building block, not a working GPU driver or an owner.

## Contract

The boot-lifetime singleton copies the owner callbacks and permits one terminal
attempt. It uses the GPLL0/counter serialization lock and rejects invalid thread
context, recursive entry and a second attempt without changing the output.
The owner and callback context must remain valid through retained uncertainty.

The platform must provide real `prepare`, `validate`, `vote` and `finish`
callbacks. Preparation acquires cold resources and supplies actual inherited
CX/MX requests, decoded command-db metadata, an already measured parent and
held CXO. There is still no production implementation of that owner. A software
request snapshot, nominal PLL calculation or empty TCS cannot supply it.
Parent hold/measurement must finish before entering this API; callbacks cannot
reenter the same serialized parent APIs. Preparation failure must unwind partial
acquisition without hardware writes, as required by the existing transaction.
Successful parent/measurement writes must not be hidden inside preparation;
their lifetime and any uncertainty belong to the preceding real owner state.

Native preparation maps only GCC `0x100000/0x90000`, GPUCC
`0x2c90000/0x9000` and GMU `0x2c6a000/0x30000`. A live owner check encloses
each map, every individual volatile read/store and each real 1/2 us delay.
Every GMU prerequisite read has its own before/after check. Even an invalid
`UINT32_MAX` bus value performs the post-load owner check before rejection.
GPUCC loads require the CFG access branch; GMU loads additionally require CX
and its access clocks. Mapping an aperture does not read its powered-down
registers. Mapping/context checks do not contain CPU bus faults or guarantee
that the underlying kernel mapper returns within a deadline.

The existing transaction preserves MX-before-CX raises, restricted CX/GPUCC
stores, bounded polling and attempted-effect accounting. The native vote
callback requires real ARC completion and validates the owner again afterward.
No GX register writes, CM3 reset release, firmware upload, PAS operation, SMMU
installation or CP submission is added. `CE_PWR_READY` here only means the
CX/GMU preparation transaction completed, not that a GPU command ran.

If mapping fails after successful acquisition, `finish(keep=0)` runs once
because no register store/vote was attempted. Any attempted transaction effect,
uncertain vote, lost lease or timeout retains all resources with `keep=1`.
The finish callback runs under the shared lock; it does not lower voltage,
release reset or attempt automatic recovery. Native mappings remain allocated
for the boot lifetime; there is no unsafe unmap on an uncertain path.

## Local Acceptance

Follow-up acceptance: `out/power-native-local-20261110-final-v2/receipt.json`
PASS additionally requires the post-map guard even when `ml_io_map` returns
null, with coincident map-failure/owner-revocation fixtures. final-v1 below is
the accepted earlier source identity, not this follow-up. The fresh four-step
`out/power-native-preflight-20261110-final-v2/receipt.json` passes all four
steps, 70 host programs and seven production-unit analyses. Both v2 receipts
bind source hashes at dirty parent `32da8e4`, not a complete XNU build.

Final receipt: `out/power-native-local-20261110-final-v1/receipt.json` PASS.

- Actual private native adapter: 403 PROT_NONE owner boundaries, fixed map
  arguments, map failures, real-delay invocation, callback mutation, recursive
  lock rejection, invalid parent, first/second vote failures and terminal state.
- Actual MMIO adapter: 582 PROT_NONE boundaries, GPUCC/GMU off-aperture faults
  prevented, independent register offsets, write restrictions and invalid-value
  post-load validation.
- Host and ASan/UBSan execution, native SWEET AArch64 compilation/assembly,
  native/host static analysis and staged-only native compilation pass. Hardware
  ACK, aperture permissions and time progression are modeled in host tests.
- `out/power-native-preflight-20261110-final-v1/receipt.json` passes all four
  local kernel prerequisite steps, 70 host programs and seven production-unit
  host analyses. Staging, SCM code generation and no-startup-call checks pass.

Preserve rejected evidence: `power-native-local-20261110-v2` and
`power-native-preflight-20261110-v1` failed when the test hardware model accessed
protected GPUCC. The model now explicitly avoids that aperture when off.
`power-native-local-20261110-v3` failed a test-helper null-pointer arithmetic
analysis; an explicit nonnull assertion fixes that precondition without
suppressing warnings. v1/v4 pass only their earlier source identities, not the
final post-load correction. Final receipts bind source hashes at dirty parent
commit `7493f87`; a later commit does not retroactively change those receipts.

## Next Integration

No complete XNU compile/link has been run for this native power batch or the
preceding ARC access correction. Both must undergo complete build and artifact
acceptance before use in an integrated candidate. No standalone CI or phone
round is needed just to exercise this unhooked adapter.

The accepted diagnostic boot remains exactly CI `42835691416 @2bf48ab` and
SHA256 `b7d88d435723fd317825050de0ed93d890fb540b79cc1bbf89c9c5209f287c44`.
It does not contain this batch, never invokes it, and must not be relabeled as
current HEAD. Wait for manual fastboot confirmation before the single
consolidated exit-tail/read-only GPU diagnostic; do not flash or auto-reboot.

Next is real shared resource ownership and inherited-vote provenance with
foreign-writer/deep-idle coordination; see `resource-owner-boundary.md` for
the distinction between suspend refusal, ordinary WFI and inherited hardware
sleep/wake state. PAS13 supported=false, secure-memory
permissions/crypto, SMMU admission and authenticated CP startup remain open.
The rejection gate and CP halt remain; GPU scratch/fence and Metal have not run.
