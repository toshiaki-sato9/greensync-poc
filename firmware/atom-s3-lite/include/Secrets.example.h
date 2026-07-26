#pragma once

#define POC_WIFI_SSID "your-wifi-ssid"
#define POC_WIFI_PW "your-wifi-password"
#define MQTT_HOST "192.0.2.10"
#define MQTT_PORT 1883

// OTA endpoints must share this HTTPS origin and path prefix.
#define OTA_BASE_URL "https://homeassistant.local:8443/greensync/ota"
#define OTA_MANIFEST_URL OTA_BASE_URL "/api/v1/channels/m5stack-atoms3-lite/stable/manifest.json"

// PEM-encoded CA certificate that signs the Firmware Server certificate.
#define OTA_CA_CERT R"PEM(
-----BEGIN CERTIFICATE-----
replace-with-local-or-public-ca-certificate
-----END CERTIFICATE-----
)PEM"
