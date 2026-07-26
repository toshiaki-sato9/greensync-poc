#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
default_firmware="firmware/atom-s3-lite/.pio/build/m5stack-atoms3/firmware.bin"

usage() {
  cat <<'EOF'
Usage:
  ./upload.sh VERSION [FIRMWARE_FILE]

Examples:
  ./upload.sh 0.3.2
  ./upload.sh 0.3.2 /private/tmp/greensync-firmware-0.3.2.bin

Uploads an already-built application binary to the stable channel on the
configured Firmware Server. This command does not send an OTA install command.
EOF
}

if (( $# < 1 || $# > 2 )); then
  usage >&2
  exit 1
fi

version="$1"
firmware_file="${2:-$default_firmware}"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Error: invalid version: $version" >&2
  exit 1
fi

if [[ "$firmware_file" != /* ]]; then
  firmware_file="$script_dir/$firmware_file"
fi

"$script_dir/scripts/upload-firmware-to-homeassistant.sh" \
  --version "$version" \
  --firmware "$firmware_file" \
  --hardware m5stack-atoms3-lite \
  --channel stable
