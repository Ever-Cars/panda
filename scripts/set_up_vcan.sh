#!/usr/bin/env sh
set -e

sudo modprobe can
sudo modprobe vcan

if ! ip link show dev vcan0 >/dev/null 2>&1; then
  sudo ip link add dev vcan0 type vcan
fi

sudo ip link set dev vcan0 up
