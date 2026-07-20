#include "MQTTService.h"
#include "FirmwareInfo.h"
#include "Secrets.h"
#include "WateringSettings.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstring>

namespace {
WiFiClient wifiClient;
PubSubClient client(wifiClient);
WateringSettings* wateringSettings = nullptr;

constexpr uint16_t MqttBufferSize = 1024;
char deviceId[40];
char deviceIdentifier[48];
char deviceName[48];
char stateTopic[80];
char thresholdStateTopic[96];
char thresholdSetTopic[96];
char firmwareVersionTopic[96];

void initializeDeviceIdentity() {
  char hardwareId[13];
  const unsigned long long chipId =
      static_cast<unsigned long long>(ESP.getEfuseMac());
  snprintf(hardwareId, sizeof(hardwareId), "%012llx", chipId);

  snprintf(deviceId, sizeof(deviceId), "greensync-atom-s3-%s", hardwareId);
  snprintf(deviceIdentifier, sizeof(deviceIdentifier),
           "greensync_atom_s3_%s", hardwareId);
  snprintf(deviceName, sizeof(deviceName), "GreenSync AtomS3 %s", hardwareId);
  snprintf(stateTopic, sizeof(stateTopic), "greensync/atom-s3-%s/state",
           hardwareId);
  snprintf(thresholdStateTopic, sizeof(thresholdStateTopic),
           "greensync/atom-s3-%s/threshold/state", hardwareId);
  snprintf(thresholdSetTopic, sizeof(thresholdSetTopic),
           "greensync/atom-s3-%s/threshold/set", hardwareId);
  snprintf(firmwareVersionTopic, sizeof(firmwareVersionTopic),
           "greensync/atom-s3-%s/ota/version", hardwareId);
}

bool publishRetained(const char* label, const char* topic, const char* payload) {
  Serial.print("MQTT publish [");
  Serial.print(label);
  Serial.print("] topic=");
  Serial.print(topic);
  Serial.print(", payloadBytes=");
  Serial.print(strlen(payload));
  Serial.print(", bufferBytes=");
  Serial.print(client.getBufferSize());
  Serial.print(", connected=");
  Serial.print(client.connected() ? "yes" : "no");

  const bool published = client.publish(topic, payload, true);
  Serial.print(", result=");
  Serial.println(published ? "OK" : "FAILED");
  return published;
}

bool publishThresholdState() {
  if (wateringSettings == nullptr) {
    Serial.println("MQTT publish [threshold state] FAILED: settings unavailable");
    return false;
  }

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"wateringThreshold\":%d}",
           wateringSettings->wateringThresholdPercent());
  return publishRetained("threshold state", thresholdStateTopic, payload);
}

bool publishFirmwareVersion() {
  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"version\":\"%s\",\"hardware\":\"%s\"}",
           FirmwareInfo::Version, FirmwareInfo::Hardware);
  return publishRetained("firmware version", firmwareVersionTopic, payload);
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  if (wateringSettings == nullptr) {
    return;
  }

  if (strcmp(topic, thresholdSetTopic) != 0) {
    return;
  }

  char buffer[16];
  const unsigned int copyLength = length < sizeof(buffer) - 1 ? length : sizeof(buffer) - 1;
  memcpy(buffer, payload, copyLength);
  buffer[copyLength] = '\0';

  const int requestedThreshold = atoi(buffer);
  const int normalizedThreshold =
      WateringSettings::clampThresholdPercent(requestedThreshold);
  const bool changed =
      wateringSettings->setWateringThresholdPercent(normalizedThreshold);

  Serial.print("Watering threshold updated via MQTT: ");
  Serial.print(normalizedThreshold);
  Serial.println(changed ? "%" : "% (unchanged)");

  publishThresholdState();
}
}

void MQTTService::begin(WateringSettings* settings) {
  wateringSettings = settings;
  initializeDeviceIdentity();
  if (!client.setBufferSize(MqttBufferSize)) {
    Serial.println("MQTT buffer allocation FAILED");
  }
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(onMessage);

  Serial.print("MQTT configuration: broker=");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.print(MQTT_PORT);
  Serial.print(", clientId=");
  Serial.print(deviceId);
  Serial.print(", bufferBytes=");
  Serial.println(client.getBufferSize());
  connect();
}

void MQTTService::loop() {
  if (!client.connected()) connect();
  client.loop();
}

void MQTTService::connect() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");
    if (client.connect(deviceId)) {
      Serial.println("connected");
      const bool subscribed = client.subscribe(thresholdSetTopic);
      Serial.print("MQTT subscribe topic=");
      Serial.print(thresholdSetTopic);
      Serial.print(", result=");
      Serial.println(subscribed ? "OK" : "FAILED");

      const bool discoveryPublished = publishDiscovery();
      Serial.print("MQTT Discovery summary=");
      Serial.println(discoveryPublished ? "ALL OK" : "FAILED");
      publishFirmwareVersion();
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

bool MQTTService::publishDiscovery() {
  Serial.println(">>> publishDiscovery called");
  Serial.print("MQTT connected=");
  Serial.println(client.connected());

  char moistureConfigTopic[128];
  snprintf(moistureConfigTopic, sizeof(moistureConfigTopic),
           "homeassistant/sensor/%s/moisture/config", deviceIdentifier);

  char moistureConfig[768];
  snprintf(
      moistureConfig, sizeof(moistureConfig),
      "{\"name\":\"Soil Moisture\",\"unique_id\":\"%s_moisture\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.moisture }}\","
      "\"unit_of_measurement\":\"%s\",\"device_class\":\"moisture\","
      "\"state_class\":\"measurement\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, stateTopic, "%", deviceIdentifier, deviceName);

  const bool moistureOk =
      publishRetained("discovery moisture", moistureConfigTopic, moistureConfig);

  char wateredConfigTopic[128];
  snprintf(wateredConfigTopic, sizeof(wateredConfigTopic),
           "homeassistant/binary_sensor/%s/watered/config", deviceIdentifier);

  char wateredConfig[768];
  snprintf(
      wateredConfig, sizeof(wateredConfig),
      "{\"name\":\"Pump Active\",\"unique_id\":\"%s_pump_active\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"{{ 'ON' if value_json.watered else 'OFF' }}\","
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, stateTopic, deviceIdentifier, deviceName);

const bool wateredOk =
  publishRetained("discovery pump", wateredConfigTopic, wateredConfig);

  char rssiConfigTopic[128];
  snprintf(rssiConfigTopic, sizeof(rssiConfigTopic),
           "homeassistant/sensor/%s/rssi/config", deviceIdentifier);

  char rssiConfig[768];
  snprintf(
      rssiConfig, sizeof(rssiConfig),
      "{\"name\":\"WiFi RSSI\",\"unique_id\":\"%s_rssi\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.rssi }}\","
      "\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\","
      "\"state_class\":\"measurement\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, stateTopic, deviceIdentifier, deviceName);

const bool rssiOk =
  publishRetained("discovery rssi", rssiConfigTopic, rssiConfig);

  char thresholdConfigTopic[128];
  snprintf(thresholdConfigTopic, sizeof(thresholdConfigTopic),
           "homeassistant/number/%s/watering_threshold/config",
           deviceIdentifier);

  char thresholdConfig[768];
  snprintf(
      thresholdConfig, sizeof(thresholdConfig),
      "{\"name\":\"Watering Threshold\","
      "\"unique_id\":\"%s_watering_threshold\",\"command_topic\":\"%s\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"{{ value_json.wateringThreshold }}\","
      "\"unit_of_measurement\":\"%s\",\"min\":0,\"max\":100,\"step\":1,"
      "\"mode\":\"box\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, thresholdSetTopic, thresholdStateTopic, "%",
      deviceIdentifier, deviceName);

  const bool thresholdOk =
      publishRetained("discovery threshold", thresholdConfigTopic, thresholdConfig);

  char firmwareConfigTopic[128];
  snprintf(firmwareConfigTopic, sizeof(firmwareConfigTopic),
           "homeassistant/sensor/%s/firmware_version/config",
           deviceIdentifier);

  char firmwareConfig[768];
  snprintf(
      firmwareConfig, sizeof(firmwareConfig),
      "{\"name\":\"Firmware Version\",\"unique_id\":\"%s_firmware_version\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.version }}\","
      "\"icon\":\"mdi:chip\",\"entity_category\":\"diagnostic\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\","
      "\"sw_version\":\"%s\"}}",
      deviceIdentifier, firmwareVersionTopic, deviceIdentifier, deviceName,
      FirmwareInfo::Version);

  const bool firmwareOk =
      publishRetained("discovery firmware", firmwareConfigTopic, firmwareConfig);

  const bool thresholdStateOk = publishThresholdState();
  return moistureOk && wateredOk && rssiOk && thresholdOk && firmwareOk &&
         thresholdStateOk;
}

bool MQTTService::publishState(int raw, int moisturePercent, int rssi, bool watered) {
  char payload[320];
  snprintf(payload, sizeof(payload),
    "{\"deviceId\":\"%s\",\"firmwareVersion\":\"%s\",\"raw\":%d,\"moisture\":%d,\"rssi\":%d,\"watered\":%s,\"wateringThreshold\":%d}",
    deviceId, FirmwareInfo::Version, raw, moisturePercent, rssi,
    watered ? "true" : "false",
    wateringSettings != nullptr ? wateringSettings->wateringThresholdPercent() : 0);

  Serial.print("Publish: ");
  Serial.println(payload);
  return publishRetained("device state", stateTopic, payload);
}

bool MQTTService::publishSettings() {
  if (wateringSettings == nullptr) {
    return false;
  }

  return publishThresholdState();
}
