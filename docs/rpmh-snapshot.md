# RPMh Request Snapshot

2026-11-10: implemented an isolated, opt-in UEFI **pre-EBS software-request**
snapshot. It is not a final inherited-vote ledger, ACK, exclusive owner, PAS
admission or GPU execution candidate. No phone operation was performed.

## Boundary

Build with `--rpmh-snapshot`; runtime additionally requires the exact isolated
`cephrpmhsnap=1` profile and an all-zero 1016-byte `/chosen/cepheus-rpmh-snapshot`
property. The default SCM-pair stage/build is unchanged. No boot packager or
XNU startup/consumer hook is added by this batch.

Before the exit loop, the adapter identifies loaded RPMh, patched DAL, CmdDb
and the accepted DXE core. Full immutable text words and PE headers are compared
against pinned local artifacts, normalizing all text DIR64 fixups and the
loader-updated ImageBase. Generated proprietary byte arrays stay in ignored
`out/`, not the source repository. This is a byte identity check, not secure
boot authentication. Mismatches or ambiguous DAL candidates reject capture.

Only the two identity-bound CmdDb name/address queries are called pre-EBS.
No guessed CX/MX addresses, RPMh getter, finish/churn or SMC is used. The final
`GetMemoryMap` is followed by bounded RAM-only capture, then EBS. Capture masks
and restores local DAIF without calling a firmware service in between. A failed
EBS retry gets a fresh map and replaces the snapshot. Successful EBS only seals
and copies the static owned record; **no RPMh/DAL/pool pointer is read post-EBS**.

Capture validates map stride/alignment, nonoverlap/overflow and RAM-only WB
BootServicesCode/Data ranges, never MMIO. Actual DAL allocator pointer/table
slots are rechecked. Each array/DRV allocation must have the accepted 24-byte
pool head, type 4, exact size prefix, aligned payload size and 16-byte tail.
The current global count/pointer selects the sorted 24-byte resource array,
not a saved getter entry. Up to 512 resources and eight DRVs per rail are
accepted; cycles, aliasing, duplicate DRV IDs and malformed values reject.
All visited records and root metadata are reread for equality. Failures clear
all rail records instead of publishing a partial success.

The pure-data ABI includes explicit `phase=1` (pre-EBS), error and identity
status, CPU/ticks, EBS attempt count and corruption checksum. It copies raw
shadow/active/sleep/wake software fields. PDC is only a presence bit; its
contents are not traversed. No firmware pointers enter the property.

## Why Not Post-EBS

The initial post-EBS traversal implementation and builds were superseded.
The final memory map is obtained before exit callbacks; callbacks can resize,
release, guard/unmap or reuse pool pages. Map coverage plus a plausible head
does not prove the object is still live. Therefore the accepted implementation
does not traverse those objects after EBS. Static observations of core memory
preservation alone do not close callback, AP/foreign-writer or ABA risks.

Repeated equality and local interrupt masking are not global atomicity,
allocation provenance authentication, hardware completion or unique ownership.
Even state=0/valid/dirty=0 can be a software state written before submission or
by sleep/wake configuration. Exit callbacks can change the recorded requests.
**Never feed this snapshot directly into ARC `owner.validate` or current-vote
initialization.** Final live-map/allocation lifetime and writer exclusion remain
required for the genuine inherited ledger and production owner.

## Local Acceptance

`scripts/test-rpmh-snapshot.py` exercises the actual staged ADT parser and
firmware adapter, all-byte checksum corruption, 33 failed read boundaries,
moved/freed/malformed pools, sorted arrays, cycles/aliasing, changing records,
RAM/RP/overflow/overlap guards and four relocated text identities. A full
real-layout RAM fixture runs capture, seal and ADT copy and checks explicit
pre-EBS phase and failure zeroing. ASan/UBSan and reader static analysis pass.
The existing `scripts/test-scm-pair.py` regression also passes.

Final full **local** RELEASE CLANGPDB AArch64 build:
`out/rpmh-snapshot-pre-ebs-accepted-20261110/`.
DarwinBoot.efi is 336896 bytes, SHA256
`db654314c91a88deea98be1a057d83d8cc61c780f9ce50937c78086ee9b18624`.
Receipt `out/rpmh-snapshot-local-20261110/accepted-receipt.json` verifies every
staged driver file equals the locally built source. Build log is
`out/rpmh-snapshot-pre-ebs-accepted-20261110.log`; missing optional `.pdb` copy
is an ignored EDK make step, not a compilation/link failure.

```sh
bash scripts/build-scm-pair-firmware.sh out/NEW --rpmh-snapshot
python3 scripts/test-rpmh-snapshot.py --firmware-build out/NEW --output out/NEW-receipt.json
python3 scripts/test-scm-pair.py
```

No XNU change, image packaging, phone boot, GPU scratch/fence, Metal or PAS
authentication occurred. PAS13=false and supported=false/CP halt remain intact.
