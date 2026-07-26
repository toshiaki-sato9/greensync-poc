# GreenSync Firmware Server

Firmware Server receives immutable ESP32 application releases from the deployment script and serves hardware-specific manifests and binaries to GreenSync devices.

## API

All endpoints are mounted below `/greensync/ota` by default.

| Method | Endpoint | Authentication | Purpose |
|---|---|---|---|
| `POST` | `/admin/api/v1/releases` | Bearer token | Validate and publish a release |
| `GET` | `/api/v1/releases/{hardware}/{version}/manifest.json` | None | Immutable release manifest |
| `GET` | `/api/v1/releases/{hardware}/{version}/firmware.bin` | None | Immutable application image |
| `GET` | `/api/v1/channels/{hardware}/{channel}/manifest.json` | None | Current channel manifest |
| `GET` | `/health` | None | Process health |
| `GET` | `/ready` | None | Storage readiness |

The upload API accepts the multipart fields documented in `docs/ota-firmware-update-spec.md`. It independently checks the size, SHA-256, ESP image magic byte, identifiers, and version before publishing a release. A published hardware/version pair cannot be overwritten.

### Integrity and authenticity

The upload script calculates SHA-256 and the server independently recalculates it while streaming the upload. The server publishes only when the supplied and calculated sizes and hashes match, and writes the server-calculated SHA-256 into the manifest. Firmware downloads are protected in transit by HTTPS.

SHA-256 detects corruption or substitution when the manifest remains trusted, but it is not a digital signature. An attacker able to replace both a binary and its manifest could calculate a new hash. Production firmware therefore still requires signed manifests or image signatures verified with a device-held public key, as described in the OTA specification.

## Local deployment beside Home Assistant

Create the local configuration directories:

```bash
mkdir -p firmware-server/certs firmware-server/secrets
```

Generate a dedicated local root CA and a server certificate signed by that CA:

```bash
cd firmware-server
./generate-certificates.sh 192.168.1.35 homeassistant.local
```

This creates:

```text
firmware-server/certs/ca.crt
firmware-server/certs/ca.key
firmware-server/certs/server.crt
firmware-server/certs/server.key
```

The server certificate contains the hostname and IP used by devices in its Subject Alternative Name. Firmware Server uses `server.crt` and `server.key`; devices trust only `ca.crt`. Keep `ca.key` private and do not deploy it to devices.

Create a deployment token without committing it:

```bash
openssl rand -hex 32 > firmware-server/secrets/deployment-token.txt
chmod 600 firmware-server/secrets/deployment-token.txt
```

Create the Compose environment file:

```bash
cp firmware-server/.env.example firmware-server/.env
```

Start the server:

```bash
firmware-server/server_ctrl.sh up
```

Stop the server without deleting the persistent firmware volume:

```bash
firmware-server/server_ctrl.sh down
```

Verify readiness:

```bash
curl --cacert firmware-server/certs/ca.crt \
  https://homeassistant.local:8443/greensync/ota/ready
```

## Publish a firmware build

### Publish and deploy to one device

From the development PC, specify an already-published release version and the target device's 12-digit ID. This verifies the immutable release manifest and sends an MQTT `install` command only to that device. It does not upload or compare a local binary; use `upload.sh` first when publishing a new release.

```bash
firmware-server/deploy.sh 0.3.1 4c1f980af6e8
```

The device ID may also be passed as `atom-s3-4c1f980af6e8` or `greensync-atom-s3-4c1f980af6e8`. MQTT credentials, when required, are supplied through `GREENSYNC_MQTT_USERNAME` and `GREENSYNC_MQTT_PASSWORD`.

### Publish only

For the default AtomS3 Lite build, the repository-root convenience command only requires the release version:

```bash
./upload.sh 0.3.2
```

An alternative application binary can be supplied as the second argument. This command uploads and displays the manifest but does not send an MQTT install command.

For the standard Home Assistant deployment, run the wrapper from the development PC. Connection settings are defined at the top of the script. It fetches the server CA certificate, prompts for the deployment token without echoing it, uploads the application image, and retrieves the published manifest.

```bash
scripts/upload-firmware-to-homeassistant.sh \
  --version 0.3.1 \
  --firmware firmware/atom-s3-lite/.pio/build/m5stack-atoms3/firmware.bin
```

The lower-level procedure is shown below for other deployment environments.

Set the release version at the top of `scripts/publish-firmware.sh`, then run:

```bash
export GREENSYNC_FIRMWARE_SERVER_URL='https://homeassistant.local:8443/greensync/ota'
export GREENSYNC_FIRMWARE_SERVER_TOKEN="$(cat firmware-server/secrets/deployment-token.txt)"
export GREENSYNC_FIRMWARE_CA_CERT='firmware-server/certs/ca.crt'

scripts/publish-firmware.sh \
  firmware/atom-s3-lite/.pio/build/m5stack-atoms3 \
  --hardware m5stack-atoms3-lite \
  --channel stable
```

The resulting stable manifest is available at:

```text
https://homeassistant.local:8443/greensync/ota/api/v1/channels/m5stack-atoms3-lite/stable/manifest.json
```

## Configuration

| Environment variable | Default | Description |
|---|---|---|
| `FIRMWARE_SERVER_ADDR` | `:8080` | Listen address |
| `FIRMWARE_SERVER_DATA_DIR` | `/data` | Persistent release storage |
| `FIRMWARE_SERVER_PATH_PREFIX` | `/greensync/ota` | HTTP path prefix |
| `FIRMWARE_SERVER_PUBLIC_BASE_URL` | Required | Public HTTPS base URL written into manifests |
| `FIRMWARE_SERVER_TOKEN_FILE` | — | Preferred deployment-token file |
| `FIRMWARE_SERVER_TOKEN` | — | Deployment token fallback for development |
| `FIRMWARE_SERVER_MAX_UPLOAD_BYTES` | `4194304` | Maximum application-image size |
| `FIRMWARE_SERVER_TLS_CERT_FILE` | — | TLS certificate; must be paired with key |
| `FIRMWARE_SERVER_TLS_KEY_FILE` | — | TLS private key; must be paired with certificate |

For cloud migration, deploy the same image with a cloud `FIRMWARE_SERVER_PUBLIC_BASE_URL`. The current implementation uses filesystem storage; an object-storage implementation can be introduced behind the storage boundary without changing device URLs or the upload contract.

## Development

Run the tests in a Go container:

```bash
docker run --rm \
  -v "$PWD/firmware-server:/src" \
  -w /src \
  golang:1.24-alpine \
  go test ./...
```
