#!/usr/bin/env bash
# Create and bring up a virtual CAN interface (default vcan0).
#
# The native_sim build already targets vcan0 (app/boards/native_sim_native_64.overlay),
# but Zephyr's stock default is zcan0, so the alias is added for samples too.
set -euo pipefail

IF="${1:-vcan0}"

if ! sudo modprobe vcan; then
	echo "vcan kernel module not available." >&2
	echo "On Ubuntu: sudo apt-get install linux-modules-extra-\$(uname -r)" >&2
	echo "The stock WSL2 kernel does not ship vcan; use a VM or a custom WSL kernel." >&2
	exit 1
fi

if ! ip link show "$IF" >/dev/null 2>&1; then
	sudo ip link add dev "$IF" type vcan
fi
sudo ip link set up "$IF"
sudo ip link property add dev "$IF" altname zcan0 2>/dev/null || true

ip -details link show "$IF"
