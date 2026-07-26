#!/usr/bin/env bash

set -euo pipefail

# Release settings. Update this value before publishing a new version.
FIRMWARE_VERSION="0.3.0"

usage() {
  cat <<'EOF'
Usage:
  publish-firmware.sh DIRECTORY --hardware ID [options]

Required:
  DIRECTORY              Directory containing application firmware binaries
  --hardware ID          Manifest hardware ID, for example m5stack-atoms3-lite

Options:
  --version VERSION      Temporarily override FIRMWARE_VERSION
  --channel CHANNEL      Release channel (default: stable)
  --pattern GLOB         Binary filename pattern (default: firmware*.bin)
  --api-url URL          Firmware Server base URL
                         (default: GREENSYNC_FIRMWARE_SERVER_URL)
  --dry-run              Validate and print artifacts without uploading
  -h, --help             Show this help

Environment:
  GREENSYNC_FIRMWARE_SERVER_URL    Firmware Server URL
  GREENSYNC_FIRMWARE_SERVER_TOKEN  Bearer token for the release API
  GREENSYNC_FIRMWARE_CA_CERT       Optional CA certificate file for curl

The script posts each matching application binary to:
  <api-url>/admin/api/v1/releases

bootloader.bin and partitions.bin are always rejected because this OTA design
updates application images only.
EOF
}

fail() {
  echo "Error: $*" >&2
  exit 1
}

require_value() {
  local option="$1"
  local value="${2:-}"
  [[ -n "$value" ]] || fail "$option requires a value"
}

sha256_file() {
  local file="$1"
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
  else
    fail "sha256sum or shasum is required"
  fi
}

directory=""
hardware=""
version="$FIRMWARE_VERSION"
channel="stable"
pattern="firmware*.bin"
api_url="${GREENSYNC_FIRMWARE_SERVER_URL:-}"
dry_run=false

while (( $# > 0 )); do
  case "$1" in
    --hardware)
      require_value "$1" "${2:-}"
      hardware="$2"
      shift 2
      ;;
    --version)
      require_value "$1" "${2:-}"
      version="$2"
      shift 2
      ;;
    --channel)
      require_value "$1" "${2:-}"
      channel="$2"
      shift 2
      ;;
    --pattern)
      require_value "$1" "${2:-}"
      pattern="$2"
      shift 2
      ;;
    --api-url)
      require_value "$1" "${2:-}"
      api_url="$2"
      shift 2
      ;;
    --dry-run)
      dry_run=true
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      fail "unknown option: $1"
      ;;
    *)
      [[ -z "$directory" ]] || fail "only one DIRECTORY may be specified"
      directory="$1"
      shift
      ;;
  esac
done

[[ -n "$directory" ]] || fail "DIRECTORY is required"
[[ -d "$directory" ]] || fail "directory does not exist: $directory"
[[ -n "$hardware" ]] || fail "--hardware is required"
[[ "$hardware" =~ ^[a-z0-9][a-z0-9._-]*$ ]] || fail "invalid hardware ID: $hardware"
[[ -n "$version" ]] || fail "FIRMWARE_VERSION must not be empty"
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]] || fail "version must be SemVer-like: $version"
[[ "$channel" =~ ^[a-z0-9][a-z0-9._-]*$ ]] || fail "invalid channel: $channel"
[[ "$pattern" != */* ]] || fail "--pattern must be a filename glob, not a path"

if [[ "$dry_run" != true ]]; then
  [[ -n "$api_url" ]] || fail "--api-url or GREENSYNC_FIRMWARE_SERVER_URL is required"
  [[ "$api_url" == https://* ]] || fail "Firmware Server URL must use HTTPS"
  [[ -n "${GREENSYNC_FIRMWARE_SERVER_TOKEN:-}" ]] || fail "GREENSYNC_FIRMWARE_SERVER_TOKEN is required"
  command -v curl >/dev/null 2>&1 || fail "curl is required"
fi

shopt -s nullglob
files=( "$directory"/$pattern )
shopt -u nullglob

(( ${#files[@]} > 0 )) || fail "no files matched '$pattern' in $directory"

for file in "${files[@]}"; do
  [[ -f "$file" ]] || fail "not a regular file: $file"
  [[ ! -L "$file" ]] || fail "symbolic links are not accepted: $file"

  filename="$(basename "$file")"
  case "$filename" in
    bootloader.bin|partitions.bin)
      fail "$filename is not an OTA application image"
      ;;
  esac

  size="$(wc -c < "$file" | tr -d '[:space:]')"
  sha256="$(sha256_file "$file")"

  echo "Artifact: $filename"
  echo "  hardware: $hardware"
  echo "  version:  $version"
  echo "  channel:  $channel"
  echo "  bytes:    $size"
  echo "  sha256:   $sha256"

  if [[ "$dry_run" == true ]]; then
    echo "  result:   DRY RUN"
    continue
  fi

  endpoint="${api_url%/}/admin/api/v1/releases"
  curl_args=(
    --fail-with-body
    --silent
    --show-error
    --request POST
    --header "Authorization: Bearer ${GREENSYNC_FIRMWARE_SERVER_TOKEN}"
    --form "hardware=$hardware"
    --form "version=$version"
    --form "channel=$channel"
    --form "artifactName=$filename"
    --form "size=$size"
    --form "sha256=$sha256"
    --form "firmware=@$file;type=application/octet-stream"
  )

  if [[ -n "${GREENSYNC_FIRMWARE_CA_CERT:-}" ]]; then
    [[ -f "$GREENSYNC_FIRMWARE_CA_CERT" ]] || fail "CA certificate does not exist: $GREENSYNC_FIRMWARE_CA_CERT"
    curl_args+=( --cacert "$GREENSYNC_FIRMWARE_CA_CERT" )
  fi

  response="$(curl "${curl_args[@]}" "$endpoint")"
  echo "  result:   UPLOADED"
  [[ -z "$response" ]] || echo "  response: $response"
done
