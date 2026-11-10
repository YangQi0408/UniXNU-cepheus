# Core Pool Lifetime

2026-11-10: an isolated, locally built DXE core now records allocation lifetime
in its **actual `CoreAllocatePoolI` / `CoreFreePoolI` paths**. This is a building
block for the final RPMh collector, not that collector or a GPU boot candidate.
Sibling firmware trees and the running phone were not modified.

## Implemented

The stage archives MU_BASECORE commit
`c6d8901de2ffaabc6110c81e90a915f84795fbd5`, checks exact source hashes, and adds
bookkeeping without changing the allocator's return values, bins, guard policy,
page release, profile updates or error handling. Successful allocations record
the exact core-requested bytes, memory type and monotonic 64-bit generation.
DAL's four-byte prefix remains part of its underlying core allocation; a future
collector must translate that bound explicitly, not query the DAL payload as
though it were the pool base.

Free attempts invalidate metadata **before** head/tail reads, debug clearing,
free-list insertion, whole-page release or guard changes. A corrupt rejected
free also invalidates conservatively. Recursive OEM pool descriptor allocation
and release use the same real paths. Address reuse gets a new generation.

`CoreConvertPagesEx` invalidates overlapping allocations before conversion to
conventional memory or any attribute conversion. This covers direct page frees
that bypass `FreePool`; failed conversion may invalidate too. Bounds include
the pool header, alignment slack and tail. This is a metadata invalidation hook,
not a comprehensive CPU/page-table permission tracker.

The fixed 16384-slot table uses at most 256 probes; overflow, duplicate live
address, invalid extent or generation exhaustion sticky-reject all queries.
Tracking failure never fabricates a live allocation or changes the real
allocation outcome. The table occupies about 512 KiB of core BSS, not heap.
Page/attribute conversion scans that table; boot-time cost is not measured.

The isolated core installs a metadata query before dispatch. It only accepts
TPL_APPLICATION before `gMemoryMapTerminated`, acquires the actual pool lock,
never reads the object address and zeroes rejected outputs. Success is a
momentary bookkeeping result, **not authorization to dereference afterward**.
No default core/firmware packager or DarwinBoot/XNU consumer is changed.

## Acceptance

Local full RELEASE CLANGPDB AArch64 core compile/link:
`out/pool-lifetime-core-20261110-final/`, DxeCore.efi 204288 bytes, SHA256
`5e515f54821990d09e76ef8a2e2e44402754d12ed2d18ee238aaa28663420c8e`.
Build log is inside that directory. Earlier v1/v2 failed during stage/PCD
configuration before compiling; v3/v4 are historical, not final source identity.

`scripts/test-pool-lifetime-core.py` runs actual staged Pool.c with mocked
external page/guard/profile services, and actual `CoreConvertPagesEx` with a
bounded memory-map fixture. O0/O2 ASan/UBSan and leak checks pass: 234 core pool
allocations, 233 pool frees and 40 page releases (one direct page free).
Cases include small/large/guard/page-as-pool, OEM recursion, shared-page live
neighbors, ABA, type/size/generation/TPL/lock/EBS rejection, corrupt free,
failed allocation, PROT_NONE addresses, exact probe collision saturation,
serial overflow and head/tail-overlapping page/attribute invalidation.
The mocked guard placement is not a hardware page-table test.

Receipt `out/pool-lifetime-core-test-20261110-accepted.json` binds all staged
core source hashes, DSC, fixture, tooling, build log, compiler response, linked
map and artifact. Native complete Pool.c static analysis has no warnings;
complete Page.c has the same two pre-existing dead-store warnings as the pinned
baseline, not a warning-free claim. The existing SCM-pair regression passes.

```sh
bash scripts/build-pool-lifetime-core.sh out/NEW
python3 scripts/test-pool-lifetime-core.py --firmware-build out/NEW --output out/NEW-receipt.json
```

## Still Missing

No final EBS tail collector, global AP/firmware/secure writer exclusion,
post-callback live-map/CPU-attribute proof, frozen allocator lifetime,
inherited ARC/BCM ledger, hardware ACK or production XNU owner exists here.
Core locks are TPL/local-CPU locks, not multiprocessor exclusion. Unknown
allocation paths, direct permission changes, firmware runtime writers and
DMA remain outside this bookkeeping. Do not feed this query or the old
phase1 pre-EBS snapshot into `owner.validate` / current-vote admission.

Next is a core-owned final collector with explicit lifetime and writer
boundaries, preserving failures instead of blindly following post-EBS pointers.
PAS13=false, supported=false rejection and CP halt remain unchanged.
No firmware volume, boot package, phone operation, GPU scratch/fence or Metal
execution was performed.
