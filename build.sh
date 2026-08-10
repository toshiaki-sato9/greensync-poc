#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$script_dir/firmware/atom-s3-lite"
device_port="${1:-${GREENSYNC_SERIAL_PORT:-}}"

if [[ -n "$device_port" ]]; then
  if [[ ! -e "$device_port" ]]; then
    echo "Error: specified USB serial port does not exist: $device_port" >&2
    echo "Available serial ports:" >&2
    pio device list >&2 || true
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
    echo "No USB serial port was detected."
    echo "Building firmware without uploading."
    cd "$project_dir"
    pio run
    exit 0
  fi

  if (( ${#candidates[@]} > 1 )); then
    echo "Error: multiple USB serial ports were detected:" >&2
    printf '  %s\n' "${candidates[@]}" >&2
    echo "Specify the target port: ./build.sh /dev/cu.usbmodemXXXX" >&2
    exit 1
  fi

  device_port="${candidates[0]}"
fi

cd "$project_dir"

echo "Using serial port: $device_port"
pio run -t upload --upload-port "$device_port"
pio device monitor --port "$device_port" --baud 115200
