#!/usr/bin/env python3
"""Host-only tests for packet-boundary receive timeline accounting."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("timeline", Path(__file__).with_name("analyze-touch-soc-log.py"))
timeline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timeline)


class Timeline(unittest.TestCase):
    def fixture(self, directory, chunks, foreign=False):
        raw = b"".join(chunks)
        (directory / "boot.log").write_bytes(raw)
        packets = [{"bytes": len(chunk), "elapsed_s": i + 1, "accepted": True}
                   for i, chunk in enumerate(chunks)]
        if foreign:
            packets.insert(0, {"bytes": 999, "elapsed_s": 0.5, "accepted": False})
        (directory / "packets.jsonl").write_text("".join(json.dumps(p) + "\n" for p in packets))
        (directory / "capture.json").write_text(json.dumps({
            "start_utc": "2026-11-08T00:00:00+00:00", "packets": len(chunks),
            "boot_log_sha256": hashlib.sha256(raw).hexdigest()}))

    def test_split_lines_and_foreign_packet(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root, [b"ignored\nSWEET TOUCH HE", b"ALTH: polls=1000\n",
                                b"CEPHEUS OSM: sample25 l3 core0\nSWEET TOUCH HEALTH: polls=2000\n",
                                b"SWEET TOUCH HEALTH: polls=3000\n"], foreign=True)
            r = timeline.analyze(root)
            self.assertEqual([e["receive_elapsed_s"] for e in r["events"]], [2, 3, 3, 4])
            self.assertEqual(r["events"][0]["line"], 2)
            self.assertEqual(r["sample25_banks"], ["l3"])
            self.assertEqual(len(r["health_after_sample25"]), 1)

    def test_empty_capture(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root, [])
            r = timeline.analyze(root)
            self.assertEqual(r["events"], [])
            self.assertEqual(r["health_after_sample25"], [])

    def test_soc_kernel_times_distinct_from_receive_times(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root, [b"CEPHEUS SOC: scheduled delay_s=240 uptime_ms=17000\n",
                                b"CEPHEUS SOC: begin delay_s=240 uptime_ms=257000\n",
                                b"CEPHEUS SOC: baseline-end delay_s=240 uptime_ms=434600\n"])
            phases = timeline.analyze(root)["soc_phases"]
            self.assertEqual([p["phase"] for p in phases], ["scheduled", "begin", "baseline-end"])
            self.assertEqual([p["receive_elapsed_s"] for p in phases], [1, 2, 3])
            self.assertEqual([p["kernel_uptime_ms"] for p in phases], [17000, 257000, 434600])
            self.assertTrue(all(p["delay_s"] == 240 for p in phases))

    def test_reject_changed_raw_or_packet_lengths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.fixture(root, [b"SWEET TOUCH RAW: n=1\n"])
            original = (root / "boot.log").read_bytes()
            (root / "boot.log").write_bytes(b"X" + original[1:])
            with self.assertRaises(AssertionError):
                timeline.analyze(root)
            self.fixture(root, [b"SWEET TOUCH RAW: n=1\n"])
            (root / "packets.jsonl").write_text(json.dumps({"bytes": 1, "elapsed_s": 1, "accepted": True}) + "\n")
            with self.assertRaises(AssertionError):
                timeline.analyze(root)

    def test_crlf_utf8_and_hid_counts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            chunks = [b"ignored \xc3\xa9\r\nSWEET HID: touch 1 down\r\n",
                      b"SWEET HID: touch 1 up\r\nSWEET TOUCH RAW: n=2"]
            self.fixture(root, chunks)
            r = timeline.analyze(root)
            self.assertEqual(r["touch_hid_down"], 1)
            self.assertEqual(r["touch_hid_up"], 1)
            self.assertEqual(r["events"][-1]["receive_elapsed_s"], 2)


if __name__ == "__main__":
    unittest.main()
