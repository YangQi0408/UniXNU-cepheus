#!/usr/bin/env python3
"""Bounded, exclusive UDP console capture with packet timing evidence."""
import argparse
import hashlib
import json
import signal
import socket
import time
from datetime import datetime, timezone
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--duration", type=float, default=360)
    args = parser.parse_args()
    if args.duration <= 0:
        parser.error("duration must be positive")
    args.directory.mkdir(parents=True, exist_ok=True)
    stopped = False

    def stop(_signum, _frame):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    digest = hashlib.sha256()
    packets = byte_count = rejected = 0
    start_utc = datetime.now(timezone.utc).isoformat()
    start = time.monotonic()
    first = last = None
    # Exclusive files and socket binding prevent silently mixing test rounds.
    with (args.directory / "boot.log").open("xb", buffering=0) as raw, \
            (args.directory / "packets.jsonl").open("x", buffering=1) as timings, \
            socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024 * 1024)
        sock.bind(("0.0.0.0", 5140))
        print(f"READY udp/5140 duration={args.duration:g}s directory={args.directory}",
              flush=True)
        while not stopped:
            remaining = args.duration - (time.monotonic() - start)
            if remaining <= 0:
                break
            sock.settimeout(min(1.0, remaining))
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            elapsed = time.monotonic() - start
            accepted = addr[0] == "192.168.7.2" and bool(data)
            timings.write(json.dumps({"elapsed_s": elapsed, "source": list(addr),
                                      "bytes": len(data), "accepted": accepted}) + "\n")
            if not accepted:
                rejected += 1
                continue
            if first is None:
                first = elapsed
                print(f"FIRST packet at +{elapsed:.3f}s from {addr}", flush=True)
            last = elapsed
            packets += 1
            byte_count += len(data)
            raw.write(data)
            digest.update(data)
    receipt = {
        "start_utc": start_utc,
        "end_utc": datetime.now(timezone.utc).isoformat(),
        "elapsed_s": time.monotonic() - start,
        "requested_duration_s": args.duration,
        "stop_reason": "signal" if stopped else "duration",
        "source_filter": "192.168.7.2", "port": 5140,
        "packets": packets, "bytes": byte_count, "rejected_packets": rejected,
        "first_packet_elapsed_s": first, "last_packet_elapsed_s": last,
        "boot_log_sha256": digest.hexdigest(),
        "limitations": "UDP has no delivery guarantee; timestamps are host receive times.",
    }
    with (args.directory / "capture.json").open("x") as out:
        json.dump(receipt, out, indent=2)
        out.write("\n")
    print(json.dumps(receipt), flush=True)


if __name__ == "__main__":
    main()
