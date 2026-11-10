# GPLL0 Scope Coordination

2026-11-11: the existing GPLL0 parent adapter now carries the real participating
RPMh reservation token. This is additional local writer/idle coordination, not
a resource owner, inherited request ledger, PLL ownership or frequency proof.
No startup caller, new PLL programming, SMC or CP operation is introduced.

## Admission And Retention

The native adapter copies the owner's token, checks the actual RPMh scope
before and after mapping, and preserves the separate resource validation.
Every parent register access checks that scope before and after the volatile
load. A scope failure is sticky even if subsequent platform checks recover.

Before the first attempted vote-word store, `ceph_gpll0_retain` calls the real
`cepheus_gpu_rpmh_finish(token,1)`. That permanently retains the apps-DRV
reservation and its actual scoped idle token before MMIO. The parent rechecks
both scope and resource guard before writing. Successful effect-free adoption
also retains before publishing HELD/output. The counter's existing parent
lease inherits this check and boot-lifetime retention; counter field restoration
does not release the parent's scope.

A rejected/uncertain keep attempt triggers the resource owner's retained
callback even if no MMIO store occurred. Such failure is POISONED, not success.
Invalid admission before any keep attempt has no resource callback or effects.
Existing configuration checks, exact unrelated vote preservation, delays and
no-retry rules remain. No current requests or owner callbacks are fabricated.

Lock order is GPLL0 domain -> RPMh transport -> idle management. All three use
nonblocking admission, not a sleeping nested lock. ARC/BCM/legacy callbacks
under the RPMh lock must not enter GPLL0 or idle management; GPLL0 callbacks
must not enter another GPLL0 API, scope management or release the idle token.
The parent scope operation itself does not call an owner callback under the
RPMh lock. This is not exclusion of secure/EL2/RSC/hardware/other-DRV writers.

## Evidence

`out/gpll0-scope-local-20261111-final-v2/receipt.json` PASS, SHA256
`88f257228b5db0e6e83035ec6ec08d71fb9b9de163efbb3a6098598615054d90`.
It exercises the actual RPMh implementation with explicitly synthetic idle and
mapping callbacks. Parent tests include 97 resource-guard hold failures,
32 resource-guard check failures, every scope-check failure with PROT_NONE
revocation, missing/released token, sticky failure and retained zero-store
adoption and every counter scope-check failure with PROT_NONE revocation.
Existing counter/power/TSENS/CM3/PDC/SMMU/upload host, ASan/UBSan,
native AArch64, static analysis and independently staged native compile pass.
The pinned vendor clock-source comparison is unchanged and passes.

Early v1/v2 had unused private helper declarations in the host fixture; v3
revealed the independent memory staging script lacked the transitive public
scope header. Their failures remain archived; warnings were not disabled.
The memory staging manifest now explicitly copies that header. Preflight
final-v1 subsequently exposed a host fixture include path without `-I xnu`;
the fixture uses an explicit relative path, not a warning suppression.
Full local preflight `out/gpll0-scope-preflight-20261111-final-v2/receipt.json`
PASS, four prerequisites/77 host/seven production analyses, SHA256
`5af17ec7ea7d7e3bc0b6daf632b7764eab79da0eb506c77adba0ecaeec423de2`.
Production source did not change during this final preflight. Complete XNU
link has a separate gate, not inferred from local PASS.
GPLL0 integration was committed and pushed as `790e0e4`. CI42874717231
@091c7ff completed and passed independent kernel/RPMh/idle linked checks,
but does not contain this GPLL0 follow-on and cannot accept it.

Complete GPLL0 run `42881254237 @d5bf1dd` succeeded at 03:27:42 Asia/Shanghai.
Independent archived-source acceptance PASS: kernel 17473248 bytes, SHA256
`3dd662d8d21c6a1d35d01e5af06793e7c8e1d13c0f1f689c4db581c9ea88cf10`,
97 core/30 memory entries, no unresolved, touch/PA64/native SCM/tail PASS.
`out/kernel-verification-ci-42881254237.json`, SHA256
`3222babe60b28e6b238ae2ae8d234eee1a771f6a35f9a684bec2cbd82b9e297f`.
Actual linked RPMh/idle checks also pass; their receipts are
`out/rpmh-linked-ci-42881254237-final-v1.json` (SHA256
`1d4c17c4cdecc142c6c8256203b6460eead6935e0c3a99f17df57549862abe2d`)
and `out/idle-linked-ci-42881254237-final-v1.json` (SHA256
`c53164a41d344dc27b308e2fb6a36cc976e7c6e5e4a5f1643acb5c5fe3f4f0d4`).
GPLL0 keep-before-store and mapping scope calls were manually inspected in
`out/gpll0-scope-ci-42881254237-disassembly.txt`, SHA256
`85e7832445337b4d929ef720c3daca4b59fc24cf598ef5a044822cbd76281923`.
This is not an independent automated GPLL0 CFG proof. The kernel excludes
later `6e24284` native power-parent binding and native QMP. No packaging or
phone boot was performed. GPU and Metal have not executed.
