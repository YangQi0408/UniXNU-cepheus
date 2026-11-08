#!/usr/bin/env python3
"""Map saved console lines to host UDP receive times, without device access."""
import argparse
import hashlib
import json
import re
from datetime import datetime, timedelta
from pathlib import Path


def analyze(directory):
    raw = (directory / "boot.log").read_bytes()
    capture = json.loads((directory / "capture.json").read_text())
    start = datetime.fromisoformat(capture["start_utc"])
    packets = [json.loads(line) for line in
               (directory / "packets.jsonl").read_text().splitlines()]
    accepted = [packet for packet in packets if packet["accepted"]]
    assert sum(packet["bytes"] for packet in accepted) == len(raw)
    assert len(accepted) == capture["packets"]
    assert hashlib.sha256(raw).hexdigest() == capture["boot_log_sha256"]
    pattern = re.compile(
        r"^SWEET TOUCH (?:RAW:|HEALTH:|FIFO:|SYSINFO:|\(cepheus\): "
        r"(?:selected|active scan|powered|probe|report err|GPIO bus err))|"
        r"^SWEET HID: (?:touch|key 0x30)|^SWEET BLANK:|"
        r"^CEPHEUS (?:SOC:|GPU:|TSENS:|GPU TSENS:|OSM: sample(?:0|25) )")
    events = []
    packet_index = offset = 0
    packet_end = accepted[0]["bytes"] if accepted else 0
    for line_number, data in enumerate(raw.splitlines(keepends=True), 1):
        offset += len(data)
        while packet_index < len(accepted) - 1 and offset > packet_end:
            packet_index += 1
            packet_end += accepted[packet_index]["bytes"]
        line = data.decode("utf-8", errors="replace").rstrip("\r\n")
        if pattern.search(line):
            elapsed = accepted[packet_index]["elapsed_s"]
            events.append({"line": line_number, "receive_elapsed_s": elapsed,
                           "receive_utc": (start + timedelta(seconds=elapsed)).isoformat(),
                           "record": line})
    health = [event for event in events if event["record"].startswith("SWEET TOUCH HEALTH:")]
    samples = [event for event in events if event["record"].startswith("CEPHEUS OSM: sample25 ")]
    after = [event for event in health if samples and
             event["receive_elapsed_s"] > max(s["receive_elapsed_s"] for s in samples)]
    text = raw.decode("utf-8", errors="replace")
    phases = []
    for event in events:
        match = re.fullmatch(r"CEPHEUS SOC: (scheduled|begin|baseline-end) delay_s=(\d+) uptime_ms=(\d+)", event["record"])
        if match:
            phases.append({**event, "phase": match[1], "delay_s": int(match[2]),
                           "kernel_uptime_ms": int(match[3])})
    return {"scope": "Saved log host receive timeline; not kernel or physical action timestamps",
            "log_sha256": capture["boot_log_sha256"],
            "events": events,
            "sample25_banks": [event["record"].split()[3] for event in samples],
            "health_after_sample25": after,
            "soc_phases": phases,
            "touch_hid_down": len(re.findall(r"^SWEET HID: touch \d+ down", text, re.M)),
            "touch_hid_up": len(re.findall(r"^SWEET HID: touch \d+ up", text, re.M)),
            "limitations": ["UDP delivery is not guaranteed; line receive time is the final containing packet",
                            "Log order/receive time does not locate a controller reset or prove scanning",
                            "No automatic recovery or hardware commands were used"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = analyze(args.directory)
    with args.output.open("x") as out:
        json.dump(result, out, indent=2)
        out.write("\n")
    print(f"Saved {len(result['events'])} receive-timed records to {args.output}")


if __name__ == "__main__":
    main()
