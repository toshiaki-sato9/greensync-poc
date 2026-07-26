#!/usr/bin/env bash

set -euo pipefail

server_ip="${1:-192.168.1.35}"
server_dns="${2:-homeassistant.local}"
script_dir="$(cd "$(dirname "$0")" && pwd)"
cert_dir="$script_dir/certs"
temporary_directory="$(mktemp -d)"

cleanup() {
  rm -rf "$temporary_directory"
}
trap cleanup EXIT

for file in ca.crt ca.key server.crt server.key; do
  if [[ ! -f "$cert_dir/$file" ]]; then
    echo "Error: required certificate file does not exist: $cert_dir/$file" >&2
    exit 1
  fi
done

certificate_uid="$(stat -c '%u' "$cert_dir/server.key")"
certificate_gid="$(stat -c '%g' "$cert_dir/server.key")"
backup_dir="$cert_dir/previous/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$backup_dir"
cp "$cert_dir/server.crt" "$backup_dir/server.crt"
cp "$cert_dir/server.key" "$backup_dir/server.key"

openssl genpkey \
  -algorithm RSA \
  -pkeyopt rsa_keygen_bits:2048 \
  -out "$temporary_directory/server.key"

openssl req \
  -new \
  -sha256 \
  -key "$temporary_directory/server.key" \
  -out "$temporary_directory/server.csr" \
  -subj "/CN=$server_ip"

extensions="$temporary_directory/server-extensions.cnf"
printf '%s\n' \
  "authorityKeyIdentifier=keyid,issuer" \
  "subjectKeyIdentifier=hash" \
  "basicConstraints=critical,CA:FALSE" \
  "keyUsage=critical,digitalSignature,keyEncipherment" \
  "extendedKeyUsage=serverAuth" \
  "subjectAltName=DNS:$server_dns,DNS:$server_ip,IP:$server_ip" \
  > "$extensions"

openssl x509 \
  -req \
  -sha256 \
  -days 825 \
  -in "$temporary_directory/server.csr" \
  -CA "$cert_dir/ca.crt" \
  -CAkey "$cert_dir/ca.key" \
  -CAcreateserial \
  -out "$temporary_directory/server.crt" \
  -extfile "$extensions"

openssl verify -CAfile "$cert_dir/ca.crt" "$temporary_directory/server.crt"
install -o "$certificate_uid" -g "$certificate_gid" -m 0400 \
  "$temporary_directory/server.key" "$cert_dir/server.key"
install -o "$certificate_uid" -g "$certificate_gid" -m 0444 \
  "$temporary_directory/server.crt" "$cert_dir/server.crt"

openssl x509 -in "$cert_dir/server.crt" -noout -subject -issuer -ext subjectAltName
echo "Server certificate renewed. Previous files: $backup_dir"
echo "Restart Firmware Server with: $script_dir/server_ctrl.sh down && $script_dir/server_ctrl.sh up"
