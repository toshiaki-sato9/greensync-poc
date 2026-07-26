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

command -v openssl >/dev/null 2>&1 || {
  echo "Error: openssl is required" >&2
  exit 1
}

mkdir -p "$cert_dir"
for file in ca.crt ca.key server.crt server.key; do
  if [[ -e "$cert_dir/$file" ]]; then
    echo "Error: $cert_dir/$file already exists; back up and remove existing certificates first" >&2
    exit 1
  fi
done

openssl genpkey \
  -algorithm RSA \
  -pkeyopt rsa_keygen_bits:3072 \
  -out "$cert_dir/ca.key"

openssl req \
  -x509 \
  -new \
  -sha256 \
  -days 3650 \
  -key "$cert_dir/ca.key" \
  -out "$cert_dir/ca.crt" \
  -subj "/CN=GreenSync Local Firmware CA" \
  -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
  -addext "keyUsage=critical,keyCertSign,cRLSign" \
  -addext "subjectKeyIdentifier=hash"

openssl genpkey \
  -algorithm RSA \
  -pkeyopt rsa_keygen_bits:2048 \
  -out "$cert_dir/server.key"

openssl req \
  -new \
  -sha256 \
  -key "$cert_dir/server.key" \
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
  -out "$cert_dir/server.crt" \
  -extfile "$extensions"

chmod 400 "$cert_dir/ca.key" "$cert_dir/server.key"
chmod 444 "$cert_dir/ca.crt" "$cert_dir/server.crt"

openssl verify -CAfile "$cert_dir/ca.crt" "$cert_dir/server.crt"
openssl x509 -in "$cert_dir/server.crt" -noout -subject -issuer -ext subjectAltName

echo "Certificates generated in $cert_dir"
echo "Keep ca.key private and back it up securely."
