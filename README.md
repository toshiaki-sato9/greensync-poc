GreenSync PoC

This repository records experiments for soil moisture monitoring and automatic irrigation using M5Stack Unit Watering, ESPHome, and Home Assistant.

PoC-001:
- Measure capacitive soil moisture sensor values
- Store sensor history in Home Assistant
- Control a small pump based on soil moisture threshold
- Evaluate soil moisture recovery after watering

Firmware release uploads are documented in `docs/ota-firmware-update-spec.md`.
Use `scripts/publish-firmware.sh` to validate or upload application binaries
from a PlatformIO build directory.

The companion server is in `firmware-server/`. It can run beside Home
Assistant with Docker Compose and can later be moved to cloud infrastructure
without changing the release API.

<img width="2826" height="1460" alt="image" src="https://github.com/user-attachments/assets/0c60a154-6079-4348-8941-7845474e578e" />

<img width="1512" height="2016" alt="IMG_0359" src="https://github.com/user-attachments/assets/ab0554c5-23b5-4f76-89a1-dccd839a45af" />

