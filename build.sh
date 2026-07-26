#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$script_dir/firmware/atom-s3-lite"
default_device_port="/dev/cu.usbmodem101"
device_port="${1:-${GREENSYNC_SERIAL_PORT:-$default_device_port}}"

if [[ ! -e "$device_port" ]]; then
  echo "Error: serial port does not exist: $device_port" >&2
  echo "Connect the device and check available ports with: pio device list" >&2
  echo "Override it with: bash build.sh /dev/cu.usbmodemXXXX" >&2
  exit 1
fi

echo "Using serial port: $device_port"
cd "$project_dir"
pio run -t upload --upload-port "$device_port"
pio device monitor --port "$device_port" --baud 115200
