#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$script_dir/firmware/atom-s3-lite"
device_port="${1:-${GREENSYNC_SERIAL_PORT:-}}"

if [[ -n "$device_port" ]]; then
  if [[ ! -e "$device_port" ]]; then
    echo "Error: serial port does not exist: $device_port" >&2
    echo "Connect the device and check available ports with: pio device list" >&2
    exit 1
  fi
else
  shopt -s nullglob
  candidates=(
    /dev/cu.usbmodem*
    /dev/cu.usbserial*
    /dev/ttyACM*
    /dev/ttyUSB*
  )
  shopt -u nullglob

  if (( ${#candidates[@]} == 0 )); then
    echo "Error: no USB serial port was detected." >&2
    echo "Connect the ATOMS3 device and check available ports with: pio device list" >&2
    exit 1
  fi

  if (( ${#candidates[@]} > 1 )); then
    echo "Error: multiple USB serial ports were detected:" >&2
    printf '  %s\n' "${candidates[@]}" >&2
    echo "Specify the target port: bash build.sh /dev/cu.usbmodemXXXX" >&2
    exit 1
  fi

  device_port="${candidates[0]}"
fi

echo "Using serial port: $device_port"
cd "$project_dir"
pio run -t upload --upload-port "$device_port"
pio device monitor --port "$device_port" --baud 115200
