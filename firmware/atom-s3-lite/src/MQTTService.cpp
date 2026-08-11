#include "MQTTService.h"
#include "CalibrationService.h"
#include "Config.h"
#include "OtaService.h"
#include "PumpEvaluationService.h"
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
CalibrationService* calibration = nullptr;
PumpEvaluationService* pumpEvaluation = nullptr;

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
char calibrationCommandTopic[96];
char calibrationStateTopic[96];
char pumpEvaluationCommandTopic[96];
char pumpEvaluationStateTopic[96];
char discoveryTopic[128];
char discoveryPayload[768];
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
  snprintf(calibrationCommandTopic, sizeof(calibrationCommandTopic),
           "greensync/atom-s3-%s/calibration/command", hardwareId);
  snprintf(calibrationStateTopic, sizeof(calibrationStateTopic),
           "greensync/atom-s3-%s/calibration/state", hardwareId);
  snprintf(pumpEvaluationCommandTopic, sizeof(pumpEvaluationCommandTopic),
           "greensync/atom-s3-%s/pump-evaluation/command", hardwareId);
  snprintf(pumpEvaluationStateTopic, sizeof(pumpEvaluationStateTopic),
           "greensync/atom-s3-%s/pump-evaluation/state", hardwareId);
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
    if (calibration != nullptr &&
        (calibration->isActive() || calibration->hasPendingCommand())) {
      Serial.println("OTA command rejected: calibration is active");
    } else if (pumpEvaluation != nullptr &&
               (pumpEvaluation->isActive() ||
                pumpEvaluation->hasPendingCommand())) {
      Serial.println("OTA command rejected: pump evaluation is active");
    } else if (ota == nullptr || !ota->queueCommand(payload, length)) {
      Serial.println("OTA command rejected: updater is busy or payload is invalid");
    }
    return;
  }

  if (strcmp(topic, calibrationCommandTopic) == 0) {
    if (pumpEvaluation != nullptr &&
        (pumpEvaluation->isActive() ||
         pumpEvaluation->hasPendingCommand())) {
      Serial.println("Calibration command rejected: pump evaluation is active");
    } else if (calibration == nullptr ||
               !calibration->queueCommand(payload, length)) {
      Serial.println("Calibration command rejected: payload is invalid or pending");
    }
    return;
  }

  if (strcmp(topic, pumpEvaluationCommandTopic) == 0) {
    if ((calibration != nullptr &&
         (calibration->isActive() || calibration->hasPendingCommand())) ||
        pumpEvaluation == nullptr ||
        !pumpEvaluation->queueCommand(payload, length)) {
      Serial.println("Pump evaluation command rejected: unsafe, invalid, or busy");
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

void MQTTService::begin(WateringSettings* settings, OtaService* otaService,
                        CalibrationService* calibrationService,
                        PumpEvaluationService* pumpEvaluationService) {
  wateringSettings = settings;
  ota = otaService;
  calibration = calibrationService;
  pumpEvaluation = pumpEvaluationService;
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
  const bool calibrationSubscribed = client.subscribe(calibrationCommandTopic, 1);
  Serial.print("MQTT subscribe topic=");
  Serial.print(calibrationCommandTopic);
  Serial.print(", result=");
  Serial.println(calibrationSubscribed ? "OK" : "FAILED");
  const bool pumpEvaluationSubscribed =
      client.subscribe(pumpEvaluationCommandTopic, 1);
  Serial.print("MQTT subscribe topic=");
  Serial.print(pumpEvaluationCommandTopic);
  Serial.print(", result=");
  Serial.println(pumpEvaluationSubscribed ? "OK" : "FAILED");

  const bool versionPublished =
      publishRetained("OTA version", otaVersionTopic, Config::FirmwareVersion);

  const bool discoveryPublished = publishDiscovery();
  const bool calibrationStatePublished =
      calibration == nullptr || calibration->publishCurrentState();
  const bool pumpEvaluationStatePublished =
      pumpEvaluation == nullptr || pumpEvaluation->publishCurrentState();
  Serial.print("MQTT Discovery summary=");
  Serial.println(discoveryPublished ? "ALL OK" : "FAILED");
  return thresholdSubscribed && otaSubscribed && calibrationSubscribed &&
         pumpEvaluationSubscribed && versionPublished && discoveryPublished &&
         calibrationStatePublished && pumpEvaluationStatePublished;
}

bool MQTTService::publishDiscovery() {
  Serial.println(">>> publishDiscovery called");
  Serial.print("MQTT connected=");
  Serial.println(client.connected());

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/sensor/%s/moisture/config", deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"Soil Moisture\",\"unique_id\":\"%s_moisture\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.moisture }}\","
      "\"unit_of_measurement\":\"%s\",\"device_class\":\"moisture\","
      "\"state_class\":\"measurement\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, stateTopic, "%", deviceIdentifier, deviceName);

  const bool moistureOk =
      publishRetained("discovery moisture", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/binary_sensor/%s/watered/config", deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"Pump Active\",\"unique_id\":\"%s_pump_active\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"{{ 'ON' if value_json.watered else 'OFF' }}\","
      "\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, stateTopic, deviceIdentifier, deviceName);

const bool wateredOk =
  publishRetained("discovery pump", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/sensor/%s/rssi/config", deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"WiFi RSSI\",\"unique_id\":\"%s_rssi\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.rssi }}\","
      "\"unit_of_measurement\":\"dBm\",\"device_class\":\"signal_strength\","
      "\"state_class\":\"measurement\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, stateTopic, deviceIdentifier, deviceName);

const bool rssiOk =
  publishRetained("discovery rssi", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/number/%s/watering_threshold/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
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
  publishRetained("discovery threshold", discoveryTopic, discoveryPayload);

  const bool thresholdStateOk = publishThresholdState();

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/sensor/%s/ota_status/config", deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"OTA Status\",\"unique_id\":\"%s_ota_status\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.state }}\","
      "\"json_attributes_topic\":\"%s\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, otaStateTopic, otaStateTopic, deviceIdentifier, deviceName);
  const bool otaStatusOk =
      publishRetained("discovery OTA status", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/sensor/%s/firmware_version/config", deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"Firmware Version\",\"unique_id\":\"%s_firmware_version\","
      "\"state_topic\":\"%s\",\"entity_category\":\"diagnostic\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, otaVersionTopic, deviceIdentifier, deviceName);
  const bool otaVersionOk =
      publishRetained("discovery firmware version", discoveryTopic,
                      discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/button/%s/moisture_calibration_start/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"① 乾燥土で校正開始\","
      "\"unique_id\":\"%s_moisture_calibration_start\","
      "\"command_topic\":\"%s\",\"payload_press\":\"START\","
      "\"icon\":\"mdi:tune-vertical\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, calibrationCommandTopic, deviceIdentifier, deviceName);
  const bool calibrationStartOk = publishRetained(
      "discovery calibration start", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/button/%s/moisture_calibration_capture/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"② 表示された基準値を記録\","
      "\"unique_id\":\"%s_moisture_calibration_capture\","
      "\"command_topic\":\"%s\",\"payload_press\":\"CAPTURE\","
      "\"icon\":\"mdi:content-save-check\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, calibrationCommandTopic, deviceIdentifier, deviceName);
  const bool calibrationCaptureOk = publishRetained(
      "discovery calibration capture", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/button/%s/moisture_calibration_cancel/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"校正を中止\","
      "\"unique_id\":\"%s_moisture_calibration_cancel\","
      "\"command_topic\":\"%s\",\"payload_press\":\"CANCEL\","
      "\"icon\":\"mdi:cancel\",\"entity_category\":\"config\","
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, calibrationCommandTopic, deviceIdentifier, deviceName);
  const bool calibrationCancelOk = publishRetained(
      "discovery calibration cancel", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/sensor/%s/moisture_calibration_status/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"次に行う校正操作\","
      "\"unique_id\":\"%s_moisture_calibration_status\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.message }}\","
      "\"json_attributes_topic\":\"%s\",\"icon\":\"mdi:progress-wrench\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, calibrationStateTopic, calibrationStateTopic,
      deviceIdentifier, deviceName);
  const bool calibrationStatusOk = publishRetained(
      "discovery calibration status", discoveryTopic, discoveryPayload);

  const char* evaluationObjectIds[] = {
      "pump_test_a", "pump_test_b", "pump_test_c",
      "pump_test_d", "pump_test_e", "pump_test_f"};
  const char* evaluationNames[] = {
      "個体差評価 31%・30秒", "個体差評価 32%・30秒",
      "個体差評価 33%・30秒", "個体差評価 34%・30秒",
      "個体差評価 35%・30秒", "個体差評価 36%・30秒"};
  const char* evaluationCommands[] = {
      "TEST_31", "TEST_32", "TEST_33", "TEST_34", "TEST_35", "TEST_36"};
  bool pumpEvaluationDiscoveryOk = true;
  for (int i = 0; i < 6; ++i) {
    snprintf(discoveryTopic, sizeof(discoveryTopic),
             "homeassistant/button/%s/%s/config", deviceIdentifier,
             evaluationObjectIds[i]);
    snprintf(
        discoveryPayload, sizeof(discoveryPayload),
        "{\"name\":\"%s\",\"unique_id\":\"%s_%s\","
        "\"command_topic\":\"%s\",\"payload_press\":\"%s\","
        "\"icon\":\"mdi:test-tube\",\"entity_category\":\"config\","
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
        "\"manufacturer\":\"GreenSync\","
        "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
        evaluationNames[i], deviceIdentifier, evaluationObjectIds[i],
        pumpEvaluationCommandTopic, evaluationCommands[i], deviceIdentifier,
        deviceName);
    pumpEvaluationDiscoveryOk =
        publishRetained("discovery pump evaluation", discoveryTopic,
                        discoveryPayload) &&
        pumpEvaluationDiscoveryOk;
  }

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/button/%s/pump_test_cancel/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"ポンプ評価を緊急中止\","
      "\"unique_id\":\"%s_pump_test_cancel\","
      "\"command_topic\":\"%s\",\"payload_press\":\"CANCEL\","
      "\"icon\":\"mdi:stop-circle\",\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\",\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, pumpEvaluationCommandTopic, deviceIdentifier,
      deviceName);
  const bool pumpEvaluationCancelOk = publishRetained(
      "discovery pump evaluation cancel", discoveryTopic, discoveryPayload);

  snprintf(discoveryTopic, sizeof(discoveryTopic),
           "homeassistant/sensor/%s/pump_test_status/config",
           deviceIdentifier);
  snprintf(
      discoveryPayload, sizeof(discoveryPayload),
      "{\"name\":\"ポンプ評価状態\","
      "\"unique_id\":\"%s_pump_test_status\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.message }}\","
      "\"json_attributes_topic\":\"%s\",\"icon\":\"mdi:water-pump\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      deviceIdentifier, pumpEvaluationStateTopic, pumpEvaluationStateTopic,
      deviceIdentifier, deviceName);
  const bool pumpEvaluationStatusOk = publishRetained(
      "discovery pump evaluation status", discoveryTopic, discoveryPayload);

  return moistureOk && wateredOk && rssiOk && thresholdOk && thresholdStateOk &&
         otaStatusOk && otaVersionOk && calibrationStartOk &&
         calibrationCaptureOk &&
         calibrationCancelOk && calibrationStatusOk &&
         pumpEvaluationDiscoveryOk && pumpEvaluationCancelOk &&
         pumpEvaluationStatusOk;
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

bool MQTTService::publishCalibrationState(const char* state, int dryRaw,
                                          int wetRaw, int pendingDryRaw,
                                          int sampleRaw,
                                          const char* message) {
  JsonDocument document;
  document["state"] = state;
  document["dryRaw"] = dryRaw;
  document["wetRaw"] = wetRaw;
  if (pendingDryRaw > 0) document["pendingDryRaw"] = pendingDryRaw;
  if (sampleRaw >= 0) document["sampleRaw"] = sampleRaw;
  document["message"] = message;

  char payload[384];
  if (serializeJson(document, payload, sizeof(payload)) == 0) return false;
  return publishRetained("calibration state", calibrationStateTopic, payload);
}

bool MQTTService::publishPumpEvaluationState(
    const char* state, const char* profile, int dutyPercent,
    int pwmFrequencyHz, int durationMs, const char* message) {
  JsonDocument document;
  document["state"] = state;
  document["profile"] = profile;
  document["dutyPercent"] = dutyPercent;
  document["pwmFrequencyHz"] = pwmFrequencyHz;
  document["durationMs"] = durationMs;
  document["automaticWateringEnabled"] = Config::AutomaticWateringEnabled;
  document["message"] = message;

  char payload[512];
  if (serializeJson(document, payload, sizeof(payload)) == 0) return false;
  return publishRetained("pump evaluation state", pumpEvaluationStateTopic,
                         payload);
}
