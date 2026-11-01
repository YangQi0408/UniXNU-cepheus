import importlib.util
import pathlib
import sys
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
        with self.assertRaises(control.ProtocolError):
            control.unpack(wire[:-1] + bytes([wire[-1] ^ 1]), self.key)

    def test_malformed_and_replay_are_refused(self):
        responder = control.Responder(self.key, enabled=True, entropy_ready=True)
        gates = control.GateState(enabled=True, entropy_ready=True)
        packet = control.Packet(control.STATUS, 1, responder.challenge, b"n" * 16)
        wire = control.pack(packet, self.key)
        self.assertEqual(responder.handle(wire[:-2], gates)[0], control.MALFORMED)
        self.assertEqual(responder.handle(wire, gates)[0], control.OK)
        self.assertEqual(responder.handle(wire, gates)[0], control.REPLAY)

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
        self.assertEqual(responder.handle(control.pack(first, self.key), gates)[0], 9)
        self.assertEqual(responder.handle(control.pack(second, self.key), gates)[0], control.UNSUPPORTED)
        self.assertEqual(len(calls), 1)

    def test_concurrent_commit_attempts_call_callback_once(self):
        started = threading.Event()
        release = threading.Event()
        calls = []

        def callback():
            calls.append(1)
            started.set()
            release.wait(1)
            return 9

        responder = control.Responder(self.key, enabled=True, entropy_ready=True,
                                      commit=callback)
        gates = control.GateState(enabled=True, entropy_ready=True,
                                  storage_clean=True, workers_quiesced=True,
                                  reset_backend=True)
        packets = [control.pack(control.Packet(control.COMMIT, n, responder.challenge,
                                                bytes([n]) * 16), self.key)
                   for n in (20, 21)]
        results = []
        first = threading.Thread(target=lambda: results.append(responder.handle(packets[0], gates)[0]))
        first.start()
        self.assertTrue(started.wait(1))
        second_result = responder.handle(packets[1], gates)[0]
        release.set()
        first.join(1)
        self.assertEqual(second_result, control.UNSUPPORTED)
        self.assertEqual(results, [9])
        self.assertEqual(len(calls), 1)

    def test_no_entropy_or_key_and_duplicate_commit_fail(self):
        responder = control.Responder(self.key, enabled=True, entropy_ready=False,
                                      commit=lambda: 0)
        gates = control.GateState(enabled=True, entropy_ready=False,
                                  storage_clean=True, workers_quiesced=True,
                                  reset_backend=True)
        p = control.Packet(control.COMMIT, 1, responder.challenge, b"a" * 16)
        self.assertEqual(responder.handle(control.pack(p, self.key), gates)[0], control.NOT_READY)
        with self.assertRaises(ValueError):
            control.Responder(b"short")
        responder._entropy_ready = True
        gates.entropy_ready = True
        self.assertEqual(responder.handle(control.pack(p, self.key), gates)[0], control.REPLAY)
        p2 = control.Packet(control.COMMIT, 2, responder.challenge, b"b" * 16)
        self.assertEqual(responder.handle(control.pack(p2, self.key), gates)[0], control.OK)
        p3 = control.Packet(control.COMMIT, 3, responder.challenge, b"c" * 16)
        self.assertEqual(responder.handle(control.pack(p3, self.key), gates)[0], control.UNSUPPORTED)

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
