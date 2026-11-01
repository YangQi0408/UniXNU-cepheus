import importlib.util
import pathlib
import sys
import subprocess
import tempfile
import threading
import unittest

path = pathlib.Path(__file__).parents[1] / "scripts" / "fastboot_control.py"
spec = importlib.util.spec_from_file_location("control", path)
control = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = control
spec.loader.exec_module(control)


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.key = bytes(range(32))

    def test_interoperable_packet_and_hmac(self):
        packet = control.Packet(control.DRY_RUN, 3, b"c" * 16, b"n" * 16)
        wire = control.pack(packet, self.key)
        self.assertEqual(control.unpack(wire, self.key), packet)
        known = control.Packet(control.STATUS, 1, bytes(range(16)), bytes(range(16, 32)))
        self.assertEqual(control.pack(known, self.key).hex(),
            "5346423101010001000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1fe201c4b1c4eff67ae4b7422c22e7e2a1eba0d9f59f5f03cb21228e0b03debcd0")
        with self.assertRaises(control.ProtocolError):
            control.unpack(wire[:-1] + bytes([wire[-1] ^ 1]), self.key)

    def test_c_codec_uses_the_same_authenticated_wire_vector(self):
        root = pathlib.Path(__file__).parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "fastboot-c-test"
            command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=undefined", str(root / "xnu/test_cepheus_fastboot.c"),
                       "-o", str(binary)]
            if sys.platform != "darwin":
                command.append("-lcrypto")
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("C/Python vector", result.stdout)

    def test_malformed_and_replay_are_refused(self):
        responder = control.Responder(self.key, enabled=True, entropy_ready=True)
        gates = control.GateState(enabled=True, entropy_ready=True)
        packet = control.Packet(control.STATUS, 1, responder.challenge, b"n" * 16)
        wire = control.pack(packet, self.key)
        self.assertEqual(responder.handle(wire[:-2], gates)[0], control.MALFORMED)
        self.assertEqual(responder.handle(wire, gates)[0], control.OK)
        self.assertEqual(responder.handle(wire, gates)[0], control.REPLAY)
        bad = control.pack(control.Packet(control.CHALLENGE, 9, responder.challenge, b"x" * 16), self.key)
        self.assertEqual(responder.handle(bad, gates)[0], control.MALFORMED)

    def test_host_client_and_responder_interoperate(self):
        responder = control.Responder(self.key)
        gates = control.GateState()

        class Datagram:
            def __init__(self):
                self.queue = []
                self.host_peer = "192.168.7.1"
            def settimeout(self, timeout):
                self.timeout = timeout
            def sendto(self, data, address):
                # The destination is the phone; recvfrom's peer is the host.
                reply = responder.handle_datagram(data, gates, peer=self.host_peer)
                if reply:
                    self.queue.append((reply, self.host_peer))
            def recvfrom(self, size):
                return self.queue.pop(0)

        client = control.Client(("192.168.7.2", 5141), self.key, sock=Datagram())
        status, body = client.request(control.STATUS)
        self.assertEqual(status, control.OK)
        self.assertEqual(len(body), 7)
        self.assertEqual(responder.handle_datagram(b"bad", gates, peer="192.168.7.99"), b"")

    def test_commit_callback_failure_is_latched(self):
        calls = []
        responder = control.Responder(self.key, enabled=True, entropy_ready=True,
                                      commit=lambda: calls.append(1) or 9)
        gates = control.GateState(enabled=True, entropy_ready=True,
                                  storage_clean=True, workers_quiesced=True,
                                  reset_backend=True)
        first = control.Packet(control.COMMIT, 10, responder.challenge, b"a" * 16)
        second = control.Packet(control.COMMIT, 11, responder.challenge, b"b" * 16)
        self.assertEqual(responder.handle(control.pack(first, self.key), gates)[0], control.UNSUPPORTED)
        self.assertEqual(responder.handle(control.pack(second, self.key), gates)[0], control.UNSUPPORTED)
        self.assertEqual(len(calls), 0)

    def test_concurrent_commit_attempts_are_refused_without_callback(self):
        calls = []
        responder = control.Responder(self.key, enabled=True, entropy_ready=True,
                                      commit=lambda: calls.append(1) or 9)
        gates = control.GateState(enabled=True, entropy_ready=True,
                                  storage_clean=True, workers_quiesced=True,
                                  reset_backend=True)
        packets = [control.pack(control.Packet(control.COMMIT, n, responder.challenge,
                                                bytes([n]) * 16), self.key)
                   for n in (20, 21)]
        results = [responder.handle(packet, gates)[0] for packet in packets]
        self.assertEqual(results, [control.UNSUPPORTED, control.UNSUPPORTED])
        self.assertEqual(calls, [])

    def test_no_entropy_or_key_and_commit_refusal(self):
        responder = control.Responder(self.key, enabled=True, entropy_ready=False,
                                      commit=lambda: 0)
        gates = control.GateState(enabled=True, entropy_ready=False,
                                  storage_clean=True, workers_quiesced=True,
                                  reset_backend=True)
        p = control.Packet(control.COMMIT, 1, responder.challenge, b"a" * 16)
        self.assertEqual(responder.handle(control.pack(p, self.key), gates)[0], control.UNSUPPORTED)
        with self.assertRaises(ValueError):
            control.Responder(b"short")

    def test_client_retries_after_server_challenge_refresh(self):
        responder = control.Responder(self.key)
        gates = control.GateState()
        old_challenge = responder.challenge
        for number in range(control.MAX_REQUESTS_PER_CHALLENGE):
            request = control.Packet(control.STATUS, number + 1, old_challenge,
                                     number.to_bytes(16, "big"))
            responder.handle(control.pack(request, self.key), gates)

        class Datagram:
            def __init__(self): self.queue = []
            def settimeout(self, timeout): pass
            def sendto(self, data, address):
                reply = responder.handle_datagram(data, gates, peer="192.168.7.1")
                if reply: self.queue.append((reply, "192.168.7.2"))
            def recvfrom(self, size): return self.queue.pop(0)

        client = control.Client(("192.168.7.2", 5141), self.key, sock=Datagram())
        client.challenge = old_challenge
        status, body = client.request(control.STATUS)
        self.assertEqual(status, control.OK)
        self.assertEqual(len(body), 7)
        self.assertNotEqual(client.challenge, old_challenge)

    def test_challenge_refreshes_after_bounded_request_budget(self):
        responder = control.Responder(self.key)
        gates = control.GateState()
        old_challenge = responder.challenge
        for number in range(control.MAX_REQUESTS_PER_CHALLENGE):
            packet = control.Packet(control.STATUS, number + 1, old_challenge,
                                    number.to_bytes(16, "big"))
            self.assertEqual(responder.handle(control.pack(packet, self.key), gates)[0], control.OK)
        packet = control.Packet(control.STATUS, 65, old_challenge, b"r" * 16)
        status, _ = responder.handle(control.pack(packet, self.key), gates)
        self.assertEqual(status, control.BUSY)
        self.assertNotEqual(responder.challenge, old_challenge)
        self.assertEqual(len(responder._seen), 0)

    def test_dry_run_cannot_be_laundered_by_caller_gate_flags(self):
        responder = control.Responder(self.key, enabled=False, entropy_ready=False)
        ready = control.GateState(enabled=True, entropy_ready=True,
            storage_clean=True, workers_quiesced=True, reset_backend=True)
        packet = control.Packet(control.DRY_RUN, 46, responder.challenge, b"q" * 16)
        status, bits = responder.handle(control.pack(packet, self.key), ready)
        self.assertEqual(status, control.NOT_READY)
        self.assertEqual(bits, bytes((0, 0, 1, 1, 1, 0, 0)))

    def test_dry_run_reports_not_ready_until_all_read_only_gates_are_true(self):
        responder = control.Responder(self.key, enabled=True, entropy_ready=True)
        packet = control.Packet(control.DRY_RUN, 44, responder.challenge, b"z" * 16)
        gates = control.GateState(enabled=True, entropy_ready=True)
        status, bits = responder.handle(control.pack(packet, self.key), gates)
        self.assertEqual(status, control.NOT_READY)
        self.assertEqual(bits, bytes((1, 1, 0, 0, 0, 0, 0)))
        packet = control.Packet(control.DRY_RUN, 45, responder.challenge, b"y" * 16)
        gates.storage_clean = True
        gates.workers_quiesced = True
        gates.reset_backend = True
        status, bits = responder.handle(control.pack(packet, self.key), gates)
        self.assertEqual(status, control.OK)
        self.assertEqual(bits, bytes((1, 1, 1, 1, 1, 0, 0)))

    def test_disabled_busy_dirty_and_failed_worker_are_not_success(self):
        responder = control.Responder(self.key, enabled=False, entropy_ready=True,
                                      commit=lambda: 0)
        base = dict(entropy_ready=True, storage_clean=True,
                    workers_quiesced=True, reset_backend=True)
        for number, state in enumerate((
                control.GateState(**base),
                control.GateState(enabled=True, failed_worker=True, **base),
                control.GateState(enabled=True, storage_clean=False, **{k: v for k, v in base.items() if k != "storage_clean"}),
        ), 1):
            packet = control.Packet(control.COMMIT, number, responder.challenge,
                                    bytes([number]) * 16)
            self.assertNotEqual(responder.handle(control.pack(packet, self.key), state)[0], control.OK)


if __name__ == "__main__":
    unittest.main()
