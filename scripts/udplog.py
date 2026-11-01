#!/usr/bin/env python3
"""Receive the phone's live kernel console over USB Ethernet.

ios-sweet patches 0075-0077 make the kernel bring up a CDC-ECM gadget
(sweetusb=2) and stream new console bytes as UDP datagrams to
192.168.7.1:5140 every 100 ms. This listens on that port and appends to a
file, so the whole boot can be read afterwards instead of photographing the
panel.

The interface address must already be set on the host:

    sudo ip addr replace 192.168.7.1/24 dev enp4s0f3u2
    sudo ip link set enp4s0f3u2 up

then, without sudo:

    python3 udplog.py boot.log
"""
import socket
import sys
import time

PORT = 5140


def main(argv):
    path = argv[1] if len(argv) > 1 else "boot.log"
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", PORT))
    sock.settimeout(2.0)
    print(f"listening on udp/{PORT}, appending to {path}", file=sys.stderr)

    received = 0
    idle = 0.0
    with open(path, "ab", buffering=0) as out:
        while True:
            try:
                data, addr = sock.recvfrom(2048)
            except socket.timeout:
                idle += 2.0
                if received and idle >= 30.0:
                    print(f"no data for 30 s after {received} datagrams; stopping",
                          file=sys.stderr)
                    return 0
                continue
            if not data:
                continue
            idle = 0.0
            received += 1
            out.write(data)
            if received == 1:
                print(f"first datagram from {addr[0]}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
