#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$script_dir/firmware/atom-s3-lite"
default_device_port="/dev/cu.usbmodem101"
default_baud_rate="115200"
device_port="${1:-${GREENSYNC_SERIAL_PORT:-$default_device_port}}"
baud_rate="${2:-${GREENSYNC_SERIAL_BAUD:-$default_baud_rate}}"

if [[ ! -e "$device_port" ]]; then
  echo "Error: USB serial port is not connected: $device_port" >&2
  echo "Available serial ports:" >&2
  pio device list >&2 || true
  exit 1
fi

echo "Opening GreenSync serial monitor"
echo "  port: $device_port"
echo "  baud: $baud_rate"
echo "Press Ctrl+C to exit."

cd "$project_dir"
exec pio device monitor --port "$device_port" --baud "$baud_rate"
