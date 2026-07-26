#!/usr/bin/env bash

set -euo pipefail

# Deployment defaults. Environment variables with the same purpose can override
# the firmware path and MQTT connection without editing this file.
DEFAULT_FIRMWARE_FILE="firmware/atom-s3-lite/.pio/build/m5stack-atoms3/firmware.bin"
HARDWARE_ID="m5stack-atoms3-lite"
CHANNEL="stable"
MQTT_HOST="${GREENSYNC_MQTT_HOST:-192.168.1.35}"
MQTT_PORT="${GREENSYNC_MQTT_PORT:-1883}"
FIRMWARE_SERVER_URL="${GREENSYNC_FIRMWARE_SERVER_URL:-https://192.168.1.35:8443/greensync/ota}"

usage() {
  cat <<'EOF'
Usage:
  firmware-server/deploy.sh VERSION DEVICE_ID

Arguments:
  VERSION    Release version, for example 0.3.1
  DEVICE_ID  Atom device ID in one of these forms:
             4c1f980af6e8
             atom-s3-4c1f980af6e8
             greensync-atom-s3-4c1f980af6e8

Environment:
  GREENSYNC_FIRMWARE_FILE          Application binary path
  GREENSYNC_FIRMWARE_SERVER_TOKEN  Optional; prompted when unset
  GREENSYNC_MQTT_HOST              MQTT broker host (default: 192.168.1.35)
  GREENSYNC_MQTT_PORT              MQTT broker port (default: 1883)
  GREENSYNC_MQTT_USERNAME          Optional MQTT username
  GREENSYNC_MQTT_PASSWORD          Optional MQTT password
EOF
}

if (( $# != 2 )); then
  usage >&2
  exit 1
fi

version="$1"
device_id="$2"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Error: invalid version: $version" >&2
  exit 1
fi

device_id="${device_id#greensync-}"
device_id="${device_id#atom-s3-}"
if [[ ! "$device_id" =~ ^[0-9a-fA-F]{12}$ ]]; then
  echo "Error: DEVICE_ID must contain exactly 12 hexadecimal characters" >&2
  exit 1
fi
device_id="$(printf '%s' "$device_id" | tr '[:upper:]' '[:lower:]')"

script_dir="$(cd "$(dirname "$0")" && pwd)"
repository_dir="$(cd "$script_dir/.." && pwd)"
uploader="$repository_dir/scripts/upload-firmware-to-homeassistant.sh"
firmware_file="${GREENSYNC_FIRMWARE_FILE:-$DEFAULT_FIRMWARE_FILE}"
if [[ "$firmware_file" != /* ]]; then
  firmware_file="$repository_dir/$firmware_file"
fi

if [[ ! -f "$firmware_file" ]]; then
  echo "Error: firmware binary does not exist: $firmware_file" >&2
  echo "Build it first with: (cd firmware/atom-s3-lite && pio run)" >&2
  exit 1
fi
if [[ ! -x "$uploader" ]]; then
  echo "Error: upload script is unavailable: $uploader" >&2
  exit 1
fi
if ! command -v mosquitto_pub >/dev/null 2>&1; then
  echo "Error: mosquitto_pub is required (macOS: brew install mosquitto)" >&2
  exit 1
fi

echo "Deploying firmware"
echo "  version:  $version"
echo "  device:   atom-s3-$device_id"
echo "  firmware: $firmware_file"

"$uploader" \
  --version "$version" \
  --firmware "$firmware_file" \
  --hardware "$HARDWARE_ID" \
  --channel "$CHANNEL"

request_id="deploy-${version}-${device_id}-$(date -u +%Y%m%dT%H%M%SZ)"
command_topic="greensync/atom-s3-${device_id}/ota/command"
manifest_url="${FIRMWARE_SERVER_URL%/}/api/v1/releases/$HARDWARE_ID/$version/manifest.json"
command_payload="{\"action\":\"install\",\"requestId\":\"$request_id\",\"targetVersion\":\"$version\",\"manifestUrl\":\"$manifest_url\"}"
mqtt_arguments=(-h "$MQTT_HOST" -p "$MQTT_PORT")

if [[ -n "${GREENSYNC_MQTT_USERNAME:-}" ]]; then
  mqtt_arguments+=(-u "$GREENSYNC_MQTT_USERNAME")
fi
if [[ -n "${GREENSYNC_MQTT_PASSWORD:-}" ]]; then
  mqtt_arguments+=(-P "$GREENSYNC_MQTT_PASSWORD")
fi

echo "Sending OTA install command..."
mosquitto_pub \
  "${mqtt_arguments[@]}" \
  -t "$command_topic" \
  -m "$command_payload"

echo "OTA command sent. Monitor progress with:"
echo "  mosquitto_sub -h $MQTT_HOST -p $MQTT_PORT -v -t 'greensync/atom-s3-$device_id/ota/#'"
