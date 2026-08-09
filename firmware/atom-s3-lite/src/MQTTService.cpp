#include "MQTTService.h"
#include "Config.h"
#include "OtaService.h"
#include "Secrets.h"
#include "WateringSettings.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace {
WiFiClient wifiClient;
PubSubClient client(wifiClient);
WateringSettings* wateringSettings = nullptr;
OtaService* ota = nullptr;

constexpr uint16_t MqttBufferSize = 1024;
char deviceId[40];
char deviceIdentifier[48];
char deviceName[48];
char stateTopic[80];
char thresholdStateTopic[96];
char thresholdSetTopic[96];
char otaCommandTopic[96];
char otaStateTopic[96];
char otaVersionTopic[96];
unsigned long lastConnectAttemptAtMs = 0;
bool connectionAttempted = false;
bool wasConnected = false;

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
  snprintf(otaCommandTopic, sizeof(otaCommandTopic),
           "greensync/atom-s3-%s/ota/command", hardwareId);
  snprintf(otaStateTopic, sizeof(otaStateTopic),
           "greensync/atom-s3-%s/ota/state", hardwareId);
  snprintf(otaVersionTopic, sizeof(otaVersionTopic),
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

void onMessage(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT receive topic=");
  Serial.print(topic);
  Serial.print(", payload=");
  for (unsigned int i = 0; i < length; ++i) {
    Serial.write(payload[i]);
  }
  Serial.println();

  if (strcmp(topic, otaCommandTopic) == 0) {
    if (ota == nullptr || !ota->queueCommand(payload, length)) {
      Serial.println("OTA command rejected: updater is busy or payload is invalid");
    }
    return;
  }

  if (wateringSettings == nullptr) {
    Serial.println("MQTT receive ignored: settings unavailable");
    return;
  }

  if (strcmp(topic, thresholdSetTopic) != 0) {
    Serial.println("MQTT receive ignored: unexpected topic");
    return;
  }

  char buffer[24];
  if (length == 0 || length >= sizeof(buffer)) {
    Serial.println("Watering threshold rejected: invalid payload length");
    publishThresholdState();
    return;
  }
  memcpy(buffer, payload, length);
  buffer[length] = '\0';

  char* end = nullptr;
  errno = 0;
  const long parsedThreshold = strtol(buffer, &end, 10);
  while (end != nullptr &&
         isspace(static_cast<unsigned char>(*end)) != 0) {
    ++end;
  }
  if (errno == ERANGE || parsedThreshold < INT_MIN || parsedThreshold > INT_MAX ||
      end == buffer || end == nullptr || *end != '\0') {
    Serial.println("Watering threshold rejected: payload must be an integer");
    publishThresholdState();
    return;
  }

  const int normalizedThreshold =
      WateringSettings::clampThresholdPercent(static_cast<int>(parsedThreshold));
  const bool changed =
      wateringSettings->setWateringThresholdPercent(normalizedThreshold);

  Serial.print("Watering threshold updated via MQTT: ");
  Serial.print(normalizedThreshold);
  Serial.println(changed ? "%" : "% (unchanged)");

  publishThresholdState();
}
}

void MQTTService::begin(WateringSettings* settings, OtaService* otaService) {
  wateringSettings = settings;
  ota = otaService;
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
}

void MQTTService::loop() {
  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const bool mqttConnected = client.connected();
  const unsigned long nowMs = millis();

  if (!wifiConnected) {
    if (mqttConnected) {
      client.disconnect();
    }
    if (wasConnected) {
      Serial.println("MQTT disconnected because WiFi is unavailable");
    }
    wasConnected = false;
    connectionAttempted = false;
    return;
  }

  if (!mqttConnected) {
    if (wasConnected) {
      Serial.println("MQTT connection lost; scheduling reconnect");
      wasConnected = false;
      connectionAttempted = false;
    }

    if (!connectionAttempted ||
        nowMs - lastConnectAttemptAtMs >=
            static_cast<unsigned long>(Config::MqttReconnectIntervalMs)) {
      connect();
    }
    return;
  }

  wasConnected = true;
  client.loop();
}

bool MQTTService::connect() {
  lastConnectAttemptAtMs = millis();
  connectionAttempted = true;

  Serial.print("MQTT connection attempt. clientId=");
  Serial.println(deviceId);
  if (!client.connect(deviceId)) {
    Serial.print("MQTT connection failed. rc=");
    Serial.println(client.state());
    return false;
  }

  wasConnected = true;
  Serial.println("MQTT connected");
  const bool thresholdSubscribed = client.subscribe(thresholdSetTopic);
  Serial.print("MQTT subscribe topic=");
  Serial.print(thresholdSetTopic);
  Serial.print(", result=");
  Serial.println(thresholdSubscribed ? "OK" : "FAILED");
  const bool otaSubscribed = client.subscribe(otaCommandTopic, 1);
  Serial.print("MQTT subscribe topic=");
  Serial.print(otaCommandTopic);
  Serial.print(", result=");
  Serial.println(otaSubscribed ? "OK" : "FAILED");

  const bool versionPublished =
      publishRetained("OTA version", otaVersionTopic, Config::FirmwareVersion);

  const bool discoveryPublished = publishDiscovery();
  Serial.print("MQTT Discovery summary=");
  Serial.println(discoveryPublished ? "ALL OK" : "FAILED");
  return thresholdSubscribed && otaSubscribed && versionPublished && discoveryPublished;
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
      "\"command_template\":\"{{ value | int }}\","
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

  const bool thresholdStateOk = publishThresholdState();

  char otaStatusConfigTopic[128];
  snprintf(otaStatusConfigTopic, sizeof(otaStatusConfigTopic),
           "homeassistant/sensor/%s/ota_status/config", deviceIdentifier);
  char otaStatusConfig[768];
  snprintf(
      otaStatusConfig, sizeof(otaStatusConfig),
      "{\"name\":\"OTA Status\",\"unique_id\":\"%s_ota_status\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.state }}\","
      "\"json_attributes_topic\":\"%s\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, otaStateTopic, otaStateTopic, deviceIdentifier, deviceName);
  const bool otaStatusOk =
      publishRetained("discovery OTA status", otaStatusConfigTopic, otaStatusConfig);

  char otaVersionConfigTopic[128];
  snprintf(otaVersionConfigTopic, sizeof(otaVersionConfigTopic),
           "homeassistant/sensor/%s/firmware_version/config", deviceIdentifier);
  char otaVersionConfig[768];
  snprintf(
      otaVersionConfig, sizeof(otaVersionConfig),
      "{\"name\":\"Firmware Version\",\"unique_id\":\"%s_firmware_version\","
      "\"state_topic\":\"%s\",\"entity_category\":\"diagnostic\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, otaVersionTopic, deviceIdentifier, deviceName);
  const bool otaVersionOk =
      publishRetained("discovery firmware version", otaVersionConfigTopic,
                      otaVersionConfig);

  return moistureOk && wateredOk && rssiOk && thresholdOk && thresholdStateOk &&
         otaStatusOk && otaVersionOk;
}

bool MQTTService::publishState(int raw, int moisturePercent, int rssi, bool watered) {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"deviceId\":\"%s\",\"raw\":%d,\"moisture\":%d,\"rssi\":%d,\"watered\":%s,\"wateringThreshold\":%d}",
    deviceId, raw, moisturePercent, rssi, watered ? "true" : "false",
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

bool MQTTService::isConnected() const { return client.connected(); }

bool MQTTService::publishOtaState(const char* state, const char* requestId,
                                  const char* targetVersion, int progress,
                                  const char* errorCode, const char* message) {
  JsonDocument document;
  document["state"] = state;
  document["requestId"] = requestId;
  document["currentVersion"] = Config::FirmwareVersion;
  document["targetVersion"] = targetVersion;
  document["progress"] = progress;
  if (errorCode != nullptr) document["errorCode"] = errorCode;
  document["message"] = message;

  char payload[512];
  if (serializeJson(document, payload, sizeof(payload)) == 0) {
    return false;
  }
  return publishRetained("OTA state", otaStateTopic, payload);
}
