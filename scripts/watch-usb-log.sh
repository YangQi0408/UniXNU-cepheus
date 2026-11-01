#!/usr/bin/env bash
# Keep the address on a reconnecting SWEET ECM interface, not other NICs.
# Run as root on the host while udplog.py listens as an ordinary user.
# Only changes the device with the exact SWEET MAC; Ctrl-C ends watching.
set -euo pipefail
[[ $(id -u) == 0 ]] || { echo 'run with sudo' >&2; exit 1; }
previous=''
while true; do
    interface=$(ip -o link | awk '/02:00:5e:77:ee:01/ {sub(/@.*/, "", $2); sub(/:$/, "", $2); print $2; exit}')
    if [[ -n $interface ]]; then
        # NetworkManager can clear a manual address during autoconfiguration.
        # Refresh only when missing, rather than assuming the interface name
        # or address survives USB reconnects.
        ip link set "$interface" up 2>/dev/null || true
        if ! ip -4 addr show dev "$interface" | grep -q 'inet 192.168.7.1/24'; then
            ip addr replace 192.168.7.1/24 dev "$interface" 2>/dev/null || true
        fi
        if [[ $interface != "$previous" ]]; then
            echo "$(date -Is): SWEET ECM on $interface; host 192.168.7.1:5140"
        fi
    elif [[ -n $previous ]]; then
        echo "$(date -Is): SWEET ECM disconnected; waiting"
    fi
    previous=$interface
    sleep 1
done
