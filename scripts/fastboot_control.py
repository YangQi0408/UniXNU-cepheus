#!/usr/bin/env python3
"""Authenticated, single-action UDP control protocol for the Cepheus port.

The protocol is deliberately useful while the platform backend is unavailable:
STATUS and DRY_RUN can be answered by a refusal-gated responder, while COMMIT
never succeeds unless every shutdown gate and a source-supported reset callback
are explicitly installed. This module does not read or print the secret.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import secrets
import socket
import struct
import threading
from dataclasses import dataclass
from typing import Callable, Optional

MAGIC = b"SFB1"
VERSION = 1
MAX_PACKET = 128
MAX_REQUESTS_PER_CHALLENGE = 64
KEY_BYTES = 32
NONCE_BYTES = 16
HEADER = struct.Struct("!4sBBH16s16s")
MAC_BYTES = hashlib.sha256().digest_size

STATUS = 1
DRY_RUN = 2
COMMIT = 3
CHALLENGE = 4
RESPONSE = 5

OK = 0
MALFORMED = 1
UNAUTHENTICATED = 2
REPLAY = 3
DISABLED = 4
NOT_READY = 5
UNSUPPORTED = 6
BUSY = 7

_ACTIONS = {STATUS, DRY_RUN, COMMIT}
_KINDS = _ACTIONS | {CHALLENGE, RESPONSE}


class ProtocolError(ValueError):
    """A packet failed bounded framing or authentication validation."""


@dataclass(frozen=True)
class Packet:
    kind: int
    sequence: int
    challenge: bytes
    client_nonce: bytes
    body: bytes = b""


def _header(kind: int, sequence: int, challenge: bytes, client_nonce: bytes) -> bytes:
    if kind not in _KINDS or not 0 <= sequence <= 0xFFFF:
        raise ProtocolError("invalid packet fields")
    if len(challenge) != NONCE_BYTES or len(client_nonce) != NONCE_BYTES:
        raise ProtocolError("invalid nonce length")
    return HEADER.pack(MAGIC, VERSION, kind, sequence, challenge, client_nonce)


def pack(packet: Packet, key: bytes) -> bytes:
    if len(key) != KEY_BYTES:
        raise ProtocolError("control key must be exactly 32 bytes")
    if len(packet.body) > MAX_PACKET - HEADER.size - MAC_BYTES:
        raise ProtocolError("packet body exceeds bounded size")
    data = _header(packet.kind, packet.sequence, packet.challenge,
                   packet.client_nonce) + packet.body
    return data + hmac.digest(key, data, "sha256")


def unpack(data: bytes, key: bytes) -> Packet:
    if len(key) != KEY_BYTES:
        raise ProtocolError("control key must be exactly 32 bytes")
    if not HEADER.size + MAC_BYTES <= len(data) <= MAX_PACKET:
        raise ProtocolError("invalid packet size")
    signed, received = data[:-MAC_BYTES], data[-MAC_BYTES:]
    expected = hmac.digest(key, signed, "sha256")
    if not hmac.compare_digest(expected, received):
        raise ProtocolError("bad authentication tag")
    magic, version, kind, sequence, challenge, client_nonce = HEADER.unpack(
        signed[:HEADER.size])
    if magic != MAGIC or version != VERSION or kind not in _KINDS:
        raise ProtocolError("invalid protocol header")
    return Packet(kind, sequence, challenge, client_nonce,
                  signed[HEADER.size:])


@dataclass
class GateState:
    """Snapshot mirrored by the XNU gate implementation."""
    enabled: bool = False
    entropy_ready: bool = False
    storage_clean: bool = False
    workers_quiesced: bool = False
    reset_backend: bool = False
    failed_worker: bool = False
    shutdown_started: bool = False

    @property
    def commit_ready(self) -> bool:
        return (self.enabled and self.entropy_ready and self.storage_clean and
                self.workers_quiesced and self.reset_backend and
                not self.failed_worker and not self.shutdown_started)


class Responder:
    """Small state machine suitable for a bounded kernel/userspace bridge."""

    def __init__(self, key: bytes, *, enabled: bool = False,
                 entropy_ready: bool = False,
                 allowed_peer: str = "192.168.7.1",
                 commit: Optional[Callable[[], int]] = None):
        if len(key) != KEY_BYTES:
            raise ValueError("control key must be exactly 32 bytes")
        self._key = bytes(key)
        self._allowed_peer = allowed_peer
        self._enabled = enabled
        self._entropy_ready = entropy_ready
        self._challenge = secrets.token_bytes(NONCE_BYTES)
        self._seen: set[tuple[int, bytes]] = set()
        self._commit = commit
        self._commit_attempted = False
        self._lock = threading.Lock()

    @property
    def challenge(self) -> bytes:
        return self._challenge

    def challenge_packet(self) -> bytes:
        return pack(Packet(CHALLENGE, 0, self._challenge, b"\0" * NONCE_BYTES), self._key)

    def handle_datagram(self, data: bytes, gates: GateState, *, peer: str) -> bytes:
        """Return a response only for the explicitly bound USB host peer."""
        if peer != self._allowed_peer:
            return b""
        try:
            packet = unpack(data, self._key)
        except ProtocolError:
            return b""
        if (packet.kind == STATUS and packet.challenge == b"\0" * NONCE_BYTES
                and packet.client_nonce == b"\0" * NONCE_BYTES):
            return self.challenge_packet()
        status, body = self.handle(data, gates)
        return pack(Packet(RESPONSE, packet.sequence, self._challenge,
                           packet.client_nonce, bytes((status,)) + body), self._key)

    def handle(self, data: bytes, gates: GateState) -> tuple[int, bytes]:
        try:
            packet = unpack(data, self._key)
        except ProtocolError:
            return MALFORMED, b""
        with self._lock:
            if packet.challenge != self._challenge:
                return REPLAY, b""
            replay_key = (packet.sequence, packet.client_nonce)
            if replay_key in self._seen:
                return REPLAY, b""
            if len(self._seen) >= MAX_REQUESTS_PER_CHALLENGE:
                return BUSY, b""
            self._seen.add(replay_key)
            if packet.kind == COMMIT:
                if not self._enabled or not gates.enabled:
                    return DISABLED, b""
                if not self._entropy_ready or not gates.entropy_ready:
                    return NOT_READY, b""
                if not gates.commit_ready:
                    return BUSY, b""
                if self._commit_attempted or self._commit is None:
                    return UNSUPPORTED, b""
                # Consume the one-shot authorization before any side effect.
                # A reset callback may return an error after initiating reset.
                self._commit_attempted = True
                callback = self._commit
            elif packet.kind == DRY_RUN:
                return (OK if gates.commit_ready else NOT_READY), _gate_bits(gates)
            else:
                return OK, _gate_bits(gates)
        result = callback()
        return (OK if not result else result), b""


def _gate_bits(gates: GateState) -> bytes:
    return bytes((int(gates.enabled), int(gates.entropy_ready),
                  int(gates.storage_clean), int(gates.workers_quiesced),
                  int(gates.reset_backend), int(gates.failed_worker),
                  int(gates.shutdown_started)))


class Client:
    """Host-side client; caller supplies an already provisioned key."""

    def __init__(self, address: tuple[str, int], key: bytes, *, timeout=1.0,
                 sock: Optional[socket.socket] = None):
        if len(key) != KEY_BYTES:
            raise ValueError("control key must be exactly 32 bytes")
        self.address, self.key, self.timeout = address, bytes(key), timeout
        self.sock = sock or socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(timeout)
        self.challenge = None
        self._sequence = 0

    def _next(self) -> int:
        self._sequence = (self._sequence + 1) & 0xFFFF
        if self._sequence == 0:
            self._sequence = 1
        return self._sequence

    def request(self, action: int) -> tuple[int, bytes]:
        if action not in _ACTIONS:
            raise ValueError("unsupported action")
        if self.challenge is None:
            hello = Packet(STATUS, 0, b"\0" * NONCE_BYTES, b"\0" * NONCE_BYTES)
            self.sock.sendto(pack(hello, self.key), self.address)
            raw, _ = self.sock.recvfrom(MAX_PACKET)
            try:
                response = unpack(raw, self.key)
            except ProtocolError as exc:
                raise ProtocolError("challenge exchange failed") from exc
            if response.kind != CHALLENGE:
                raise ProtocolError("challenge exchange returned wrong kind")
            self.challenge = response.challenge
        nonce = secrets.token_bytes(NONCE_BYTES)
        packet = Packet(action, self._next(), self.challenge, nonce)
        self.sock.sendto(pack(packet, self.key), self.address)
        raw, _ = self.sock.recvfrom(MAX_PACKET)
        if len(raw) < HEADER.size + MAC_BYTES:
            raise ProtocolError("short response")
        response = unpack(raw, self.key)
        if (response.kind != RESPONSE or len(response.body) < 1 or
                response.sequence != packet.sequence or
                response.client_nonce != packet.client_nonce or
                response.challenge != self.challenge):
            raise ProtocolError("invalid response")
        return response.body[0], response.body[1:]


def _key(path: str) -> bytes:
    with open(path, "rb") as stream:
        value = stream.read()
    if len(value) != KEY_BYTES:
        raise SystemExit("key file must contain exactly 32 bytes")
    return value


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("status", "dry-run", "commit"))
    parser.add_argument("--host", default="192.168.7.2")
    parser.add_argument("--port", type=int, default=5141)
    parser.add_argument("--key-file", required=True)
    args = parser.parse_args()
    action = {"status": STATUS, "dry-run": DRY_RUN, "commit": COMMIT}[args.action]
    status, body = Client((args.host, args.port), _key(args.key_file)).request(action)
    print(f"status={status} gate_bytes={body.hex()}")


if __name__ == "__main__":
    main()
