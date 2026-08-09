#!/usr/bin/env bash

set -euo pipefail

# Deployment defaults.
HARDWARE_ID="m5stack-atoms3-lite"
MQTT_HOST="${GREENSYNC_MQTT_HOST:-192.168.1.35}"
MQTT_PORT="${GREENSYNC_MQTT_PORT:-1883}"
FIRMWARE_SERVER_URL="${GREENSYNC_FIRMWARE_SERVER_URL:-https://192.168.1.35:8443/greensync/ota}"
HOME_ASSISTANT_SSH="${GREENSYNC_HOME_ASSISTANT_SSH:-sato@192.168.1.35}"
REMOTE_CA_CERT="${GREENSYNC_REMOTE_CA_CERT:-/opt/greensync/firmware-server/certs/ca.crt}"

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
  GREENSYNC_FIRMWARE_SERVER_URL    Firmware Server base URL
  GREENSYNC_HOME_ASSISTANT_SSH     SSH destination used to fetch the CA
  GREENSYNC_REMOTE_CA_CERT         CA certificate path on the server
  GREENSYNC_MQTT_HOST              MQTT broker host (default: 192.168.1.35)
  GREENSYNC_MQTT_PORT              MQTT broker port (default: 1883)
  GREENSYNC_MQTT_USERNAME          Optional MQTT username
  GREENSYNC_MQTT_PASSWORD          Optional MQTT password
  GREENSYNC_DEVICE_WAIT_SECONDS    Live-state wait time (default: 15)
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

for command in curl mosquitto_pub mosquitto_sub python3 scp; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Error: required command is unavailable: $command" >&2
    exit 1
  fi
done

echo "Deploying firmware"
echo "  version:  $version"
echo "  device:   atom-s3-$device_id"

request_id="deploy-${version}-${device_id}-$(date -u +%Y%m%dT%H%M%SZ)"
command_topic="greensync/atom-s3-${device_id}/ota/command"
state_topic="greensync/atom-s3-${device_id}/state"
version_topic="greensync/atom-s3-${device_id}/ota/version"
manifest_url="${FIRMWARE_SERVER_URL%/}/api/v1/releases/$HARDWARE_ID/$version/manifest.json"
command_payload="{\"action\":\"install\",\"requestId\":\"$request_id\",\"targetVersion\":\"$version\",\"manifestUrl\":\"$manifest_url\"}"
mqtt_arguments=(-h "$MQTT_HOST" -p "$MQTT_PORT")

if [[ -n "${GREENSYNC_MQTT_USERNAME:-}" ]]; then
  mqtt_arguments+=(-u "$GREENSYNC_MQTT_USERNAME")
fi
if [[ -n "${GREENSYNC_MQTT_PASSWORD:-}" ]]; then
  mqtt_arguments+=(-P "$GREENSYNC_MQTT_PASSWORD")
fi

echo "Checking device availability..."
device_wait_seconds="${GREENSYNC_DEVICE_WAIT_SECONDS:-15}"
if ! mosquitto_sub \
  "${mqtt_arguments[@]}" \
  -R \
  -C 1 \
  -W "$device_wait_seconds" \
  -t "$state_topic" \
  >/dev/null 2>&1; then
  echo "Error: device did not publish a live state within ${device_wait_seconds}s" >&2
  echo "  device: atom-s3-$device_id" >&2
  exit 1
fi

current_version="$(mosquitto_sub \
  "${mqtt_arguments[@]}" \
  -C 1 \
  -W 3 \
  -t "$version_topic" \
  2>/dev/null || true)"
if [[ -z "$current_version" ]]; then
  echo "Error: device does not advertise an OTA firmware version" >&2
  echo "  device: atom-s3-$device_id" >&2
  echo "Install an OTA-capable firmware over USB first." >&2
  exit 1
fi
echo "  current version: $current_version"
if [[ "$current_version" == "$version" ]]; then
  echo "Device already runs firmware $version; skipping OTA command."
  exit 0
fi

temporary_directory="$(mktemp -d)"
ca_certificate="$temporary_directory/server.crt"
manifest_file="$temporary_directory/manifest.json"
cleanup() {
  rm -rf "$temporary_directory"
}
trap cleanup EXIT

echo "Fetching Firmware Server CA certificate..."
scp "$HOME_ASSISTANT_SSH:$REMOTE_CA_CERT" "$ca_certificate"

echo "Checking published release..."
curl \
  --fail-with-body \
  --silent \
  --show-error \
  --cacert "$ca_certificate" \
  --output "$manifest_file" \
  "$manifest_url"

python3 - "$manifest_file" "$HARDWARE_ID" "$version" <<'PY'
import json
import pathlib
import sys

manifest_path, hardware, version = sys.argv[1:]
manifest = json.loads(pathlib.Path(manifest_path).read_text())
if manifest.get("hardware") != hardware or manifest.get("version") != version:
    print("Error: published manifest does not match the requested release", file=sys.stderr)
    raise SystemExit(1)
print(f"  published size:   {manifest.get('size')}")
print(f"  published sha256: {manifest.get('sha256')}")
PY

echo "Sending OTA install command..."
mosquitto_pub \
  "${mqtt_arguments[@]}" \
  -q 1 \
  -t "$command_topic" \
  -m "$command_payload"

echo "OTA command sent. Monitor progress with:"
echo "  mosquitto_sub -h $MQTT_HOST -p $MQTT_PORT -v -t 'greensync/atom-s3-$device_id/ota/#'"
