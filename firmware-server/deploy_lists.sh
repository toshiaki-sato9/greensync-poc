#!/usr/bin/env bash

set -o pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
deploy_script="${GREENSYNC_DEPLOY_SCRIPT:-$script_dir/deploy.sh}"
default_targets_file="$script_dir/targets.list"

usage() {
  cat <<'EOF'
Usage:
  firmware-server/deploy_lists.sh VERSION [TARGETS_FILE]

Arguments:
  VERSION       Firmware version to deploy, for example 0.3.6
  TARGETS_FILE  One Atom device ID per line
                (default: firmware-server/targets.list)

Blank lines and lines beginning with # are ignored. Device IDs may use any
form accepted by deploy.sh:
  4c1f980af6e8
  atom-s3-4c1f980af6e8
  greensync-atom-s3-4c1f980af6e8
EOF
}

if (( $# < 1 || $# > 2 )); then
  usage >&2
  exit 1
fi

version="$1"
targets_file="${2:-$default_targets_file}"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Error: invalid version: $version" >&2
  exit 1
fi
if [[ ! -x "$deploy_script" ]]; then
  echo "Error: deploy script is not executable: $deploy_script" >&2
  exit 1
fi
if [[ ! -r "$targets_file" ]]; then
  echo "Error: targets file is not readable: $targets_file" >&2
  echo "Create it from: $script_dir/targets.list.example" >&2
  exit 1
fi

targets=()
line_number=0
while IFS= read -r line || [[ -n "$line" ]]; do
  ((line_number += 1))
  line="${line%%#*}"
  line="$(printf '%s' "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  [[ -n "$line" ]] || continue

  device_id="${line#greensync-}"
  device_id="${device_id#atom-s3-}"
  if [[ ! "$device_id" =~ ^[0-9a-fA-F]{12}$ ]]; then
    echo "Error: invalid device ID at $targets_file:$line_number: $line" >&2
    exit 1
  fi
  device_id="$(printf '%s' "$device_id" | tr '[:upper:]' '[:lower:]')"

  duplicate=false
  for existing in "${targets[@]}"; do
    if [[ "$existing" == "$device_id" ]]; then
      duplicate=true
      break
    fi
  done
  if [[ "$duplicate" == false ]]; then
    targets+=("$device_id")
  fi
done < "$targets_file"

if (( ${#targets[@]} == 0 )); then
  echo "Error: no device IDs found in $targets_file" >&2
  exit 1
fi

echo "Deploying firmware to target list"
echo "  version: $version"
echo "  targets: ${#targets[@]}"
echo "  file:    $targets_file"

succeeded=0
failed=0
failed_targets=()
for device_id in "${targets[@]}"; do
  echo
  echo "===== atom-s3-$device_id ====="
  if "$deploy_script" "$version" "$device_id"; then
    ((succeeded += 1))
  else
    ((failed += 1))
    failed_targets+=("$device_id")
  fi
done

echo
echo "Deployment summary"
echo "  succeeded: $succeeded"
echo "  failed:    $failed"

if (( failed > 0 )); then
  echo "  failed targets:"
  for device_id in "${failed_targets[@]}"; do
    echo "    atom-s3-$device_id"
  done
  exit 1
fi
