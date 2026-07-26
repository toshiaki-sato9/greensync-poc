#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$script_dir/firmware/atom-s3-lite"
default_device_port="/dev/cu.usbmodem101"
device_port="${1:-${GREENSYNC_SERIAL_PORT:-$default_device_port}}"

cd "$project_dir"

if [[ ! -e "$device_port" ]]; then
  echo "USB serial port is not connected: $device_port"
  echo "Building firmware without uploading."
  pio run
  exit 0
fi

echo "Using serial port: $device_port"
pio run -t upload --upload-port "$device_port"
pio device monitor --port "$device_port" --baud 115200
