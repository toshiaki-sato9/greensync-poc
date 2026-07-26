#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
compose_file="$script_dir/compose.yaml"
environment_file="$script_dir/.env"

usage() {
  cat <<'EOF'
Usage:
  firmware-server/server_ctrl.sh up
  firmware-server/server_ctrl.sh down

Commands:
  up    Build and start Firmware Server in the background
  down  Stop and remove Firmware Server containers and network

The persistent firmware-data volume is preserved by both commands.
EOF
}

if (( $# != 1 )); then
  usage >&2
  exit 1
fi

case "$1" in
  up|down)
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    echo "Error: unknown command: $1" >&2
    usage >&2
    exit 1
    ;;
esac

if ! command -v docker >/dev/null 2>&1; then
  echo "Error: docker is required" >&2
  exit 1
fi
if [[ ! -f "$compose_file" ]]; then
  echo "Error: compose file does not exist: $compose_file" >&2
  exit 1
fi
if [[ ! -f "$environment_file" ]]; then
  echo "Error: environment file does not exist: $environment_file" >&2
  echo "Create it with: cp $script_dir/.env.example $environment_file" >&2
  exit 1
fi

compose=(
  docker compose
  --env-file "$environment_file"
  --file "$compose_file"
)

case "$1" in
  up)
    "${compose[@]}" up --detach --build
    "${compose[@]}" ps
    ;;
  down)
    "${compose[@]}" down
    ;;
esac
