# USB fastboot control readiness

This branch contains an offline, refusal-first milestone. It does **not** claim
that a Cepheus phone can reset into fastboot.

## Protocol and key boundary

`scripts/fastboot_control.py` defines UDP/IPv4 control packet framing for the
existing USB CDC-ECM network. Packets are at most 128 bytes and contain a fixed
magic/version, sequence, per-boot challenge, client nonce, and HMAC-SHA256.
The responder accepts one `(sequence, client nonce)` pair only once and caps each challenge to 64 requests. `STATUS`
and `DRY_RUN` expose bounded gate state; `COMMIT` is not a remote shell or a
MMIO command and is refused unless every gate is ready.

The host client reads a 32-byte key from a local file and never logs it. The
optional `scripts/prepare-control-key.py provision --enable ROOT` command
creates a root-owned 0600 key in a mounted staging root and refuses replacement.
It refuses `/` and roots without the iOS launchd marker, traverses the key
path through no-follow directory descriptors, and refuses symlinked descendants.
Key provisioning does not enable the service. `sweetfastboot=0` is explicitly present in `board.toml`.
No key is stored in this repository or in reports.

## XNU gate contract

`xnu/cepheus_reboot.[ch]` provides the narrow interface that a verified XNU
control bridge may call. Worker entry is rejected after shutdown begins; worker
errors latch permanently; dirty storage and active workers block quiesce; and a
reset callback must be registered exactly once. The default has no callback,
so there is no guessed PMIC, PSCI, ABL, IMEM, PS_HOLD, or direct-MMIO backend.
The callback is also single-use, preventing duplicate execute.

The source is staged into a private XNU build copy by
`xnu/0086-stage-cepheus-reboot.sh` and added to `bsd/conf/files` by
`xnu/0086-cepheus-reboot.patch`. The workflow fails if the patch is skipped or
the staged source differs. This is patch/inventory validation, not a full XNU
build.

## Acceptance boundary and blockers

Offline tests cover C/Python framing interoperability, malformed packets,
wrong HMAC, replay, missing entropy/key, disabled control, duplicate execute,
callback-error and concurrent execute latching, peer binding, symlinked key
paths, shutdown races, dirty storage, and failed workers. The workflow runs the
Python suite and C gate test. They do not prove a macOS
XNU build, XNU shutdown hook integration, reset ABI, PMIC/PSCI/ABL selection,
bootloader fastboot selection, or hardware behavior.

Before enabling `sweetfastboot`, the parent must add and independently verify:

1. a Cepheus platform reset callback with source evidence for bootloader mode;
2. joined UFS/VFS and persistent-log/peripheral worker shutdown hooks, with
   errors propagated and no reset after a dirty or failed shutdown;
3. a trusted XNU/userspace entropy-ready signal and exact key-store plumbing;
4. a real macOS XNU build and then parent-owned hardware acceptance.

Until those prerequisites exist, physical buttons remain the recovery path and
`COMMIT` must remain refused.
