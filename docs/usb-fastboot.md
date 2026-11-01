# USB fastboot control readiness

This branch contains an optional, read-only STATUS/DRY_RUN endpoint. It does **not**
claim that a Cepheus phone can reset into fastboot: COMMIT is unconditionally refused.

## Protocol and key boundary

`scripts/fastboot_control.py` defines UDP/IPv4 control packet framing for the
existing USB CDC-ECM network. Packets are at most 128 bytes and contain a fixed
magic/version, sequence, per-boot challenge, client nonce, and HMAC-SHA256.
The C and Python implementations share a fixed 40-byte header and HMAC-SHA256
wire vector. The responder accepts one `(sequence, client nonce)` pair only once
and caps each challenge to 64 requests. `STATUS` and `DRY_RUN` expose bounded
read-only state; they never stop workers, sync storage, unmount, or touch MMIO.
`COMMIT` is refused unconditionally and has no callback path.

The host client reads a 32-byte key from a local file and never logs it. The
optional `scripts/prepare-control-key.py provision --enable ROOT` command
creates a root-owned 0600 key in a mounted staging root and refuses replacement.
It refuses `/` and roots without the iOS launchd marker, traverses the key
path through no-follow directory descriptors, and refuses symlinked descendants.
`tests/test_control_key.py` also checks the XNU key-header include name and
compiles a synthetic private header probe.
Key provisioning does not enable the service. `sweetfastboot=0` is explicitly present in `board.toml`.
No key is stored in this repository or in reports.

## XNU gate contract

`xnu/cepheus_reboot.[ch]` provides the narrow interface that a verified XNU
control bridge may call. Worker entry is rejected after shutdown begins; worker
errors latch permanently; dirty storage and active workers block quiesce; and a
reset callback must be registered exactly once. The default has no callback,
so there is no guessed PMIC, PSCI, ABL, IMEM, PS_HOLD, or direct-MMIO backend.
The generic gate remains offline only; both `cepheus_reboot_authorize(COMMIT)` and
`cepheus_reboot_execute()` return `ENOTSUP` unconditionally. No endpoint path calls
prepare/stop/sync or this gate.

The source is staged into a private XNU build copy by
`xnu/0086-stage-cepheus-reboot.sh` and the endpoint by
`xnu/0087-stage-cepheus-fastboot.sh`; their source files are added to
`bsd/conf/files` by the ordered patches. The endpoint starts only after `ue0`
exists, binds UDP/IPv4 192.168.7.2:5141 to ue0, enables IP_PKTINFO, and accepts
only peer 192.168.7.1 with the matching ingress interface and destination. The workflow fails if the patch is skipped or
the staged source differs. This is patch/inventory validation, not a full XNU
build.

## Acceptance boundary and blockers

Offline tests cover C/Python framing interoperability, malformed packets,
wrong HMAC, replay, missing entropy/key, disabled control, unconditional
COMMIT refusal, peer binding, FIFO/hard-link key paths, shutdown races, dirty
storage during blocked sync, and failed-worker admission. The workflow runs the
Python suite and C gate test. They do not prove a macOS
XNU build, XNU shutdown hook integration, reset ABI, PMIC/PSCI/ABL selection,
bootloader fastboot selection, or hardware behavior.

Before enabling `sweetfastboot`, the parent must add and independently verify:

1. a Cepheus platform reset callback with source evidence for bootloader mode;
2. joined UFS/VFS and persistent-log/peripheral worker shutdown hooks, with
   errors propagated and no reset after a dirty or failed shutdown;
3. private key staging from a caller-owned key file and source-synchronized healthy entropy;
4. a real macOS XNU build and then parent-owned hardware acceptance.

Until those prerequisites exist, physical buttons remain the recovery path and
`COMMIT` must remain refused.
