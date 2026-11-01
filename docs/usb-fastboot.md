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
`DRY_RUN` always returns NOT_READY, even if a reference caller supplies ready
flags. A successful STATUS exchange proves neither clean storage nor reset
readiness. All action bodies must be empty, including the challenge hello.

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

### Endpoint lifetime and entropy

The kernel service is a **five-minute diagnostic lease**, not a persistent
recovery daemon. It admits at most32 receive attempts per one-second monotonic
window, before authentication (including malformed/unauthenticated traffic).
After exhaustion it yields; the receive buffer is4096 bytes. A fixed-window
boundary may allow two adjacent bursts. It stops after300 seconds, on fatal
socket errors or on `cepheus_fastboot_request_stop()`. The stop request is
asynchronous; `cepheus_fastboot_is_stopped()` acknowledges socket/ifnet release,
not proof of UFS or other worker quiescence. Startup is one-shot for this boot.
No lifecycle event performs a hardware reset. The socket has exactly one owning
thread; every setup/error/exit path releases the retained interface reference.

Startup waits at most60 seconds for runtime entropy. Calling read_random drives
the existing SWEET refresh; a new flag, set only AFTER successful reseeding from
at least256 credited samples, is checked under the PRNG mutex. The separate
entropy-health snapshot is also locked. A boot seed alone or timestamp is not
treated as readiness. A failed health check remains fail-closed. Challenges are
fresh per boot and after the64-request budget, not embedded per-image nonces.

### Build verification

The inventory check is portable and does not imply patch application:

```sh
python3 scripts/verify-reboot-integration.py --inventory-only
python3 scripts/verify-reboot-integration.py --xnu-tree /private/pre-feature-xnu
python3 scripts/verify-reboot-integration.py --applied-tree /private/built-xnu
```

The second command copies a few actual source files to a temporary directory,
applies contextual patches and checks their placement. It never writes the
input tree. CI checks inventory before build and the actual patched source
after build. The unprovisioned build retains the full runtime code for compile
and link validation; no real key/header is submitted to CI. Actual hardware
operation still requires a private provisioned image and separate acceptance.

The first full-build attempt41318474454 on macos-14 was cancelled before any
steps by the runner retirement brownout (actions/runner-images#13518), not by a
compiler error. The feature workflow now selects macos-15 and Xcode16.2. This
new toolchain must pass a full build; earlier Xcode15.4 success does not prove it.

## Acceptance boundary and blockers

Offline tests cover C/Python framing interoperability, malformed packets,
wrong HMAC, replay, missing entropy/key, disabled control, unconditional
COMMIT refusal, peer binding, FIFO/hard-link key paths, shutdown races, dirty
storage during blocked sync, and failed-worker admission. The workflow runs the
Python suite and C gate test. They do not prove a macOS
XNU build, XNU shutdown hook integration, reset ABI, PMIC/PSCI/ABL selection,
bootloader fastboot selection, or hardware behavior.

Before enabling the read-only diagnostic endpoint, the parent must validate a
full XNU build and privately provision the image. No public CI artifact contains
a usable control key. Private headers and binaries contain extractable secrets
and must not be uploaded. This diagnostic does not require a reset backend.

Before implementing actual reboot, the parent must add and independently verify:

1. a Cepheus platform reset callback with source evidence for bootloader mode;
2. joined UFS/VFS and persistent-log/peripheral worker shutdown hooks, with
   errors propagated and no reset after a dirty or failed shutdown;
3. private key staging from a caller-owned key file and source-synchronized healthy entropy;
4. a real macOS XNU build and then parent-owned hardware acceptance.

Until those prerequisites exist, physical buttons remain the recovery path and
`COMMIT` must remain refused.
