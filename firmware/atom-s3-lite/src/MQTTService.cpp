#include "MQTTService.h"
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

const char* DeviceId = "greensync-atom-s3-001";
const char* StateTopic = "greensync/atom-s3-001/state";
const char* ThresholdStateTopic = "greensync/atom-s3-001/threshold/state";
const char* ThresholdSetTopic = "greensync/atom-s3-001/threshold/set";

void publishThresholdState() {
  if (wateringSettings == nullptr) {
    return;
  }

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"wateringThreshold\":%d}",
           wateringSettings->wateringThresholdPercent());
  client.publish(ThresholdStateTopic, payload, true);
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  if (wateringSettings == nullptr) {
    return;
  }

  if (strcmp(topic, ThresholdSetTopic) != 0) {
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
  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(onMessage);
  connect();
}

void MQTTService::loop() {
  if (!client.connected()) connect();
  client.loop();
}

void MQTTService::connect() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");
    if (client.connect(DeviceId)) {
      Serial.println("connected");
      client.subscribe(ThresholdSetTopic);
      publishDiscovery();
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void MQTTService::publishDiscovery() {
  Serial.println(">>> publishDiscovery called");
  Serial.print("MQTT connected=");
  Serial.println(client.connected());

  const char* moistureConfigTopic =
    "homeassistant/sensor/greensync_atom_s3_001/moisture/config";

  const char* moistureConfig =
    "{\"name\":\"Soil Moisture\",\"unique_id\":\"greensync_atom_s3_001_moisture\","
    "\"state_topic\":\"greensync/atom-s3-001/state\",\"value_template\":\"{{ value_json.moisture }}\","
    "\"unit_of_measurement\":\"%\",\"device_class\":\"moisture\",\"state_class\":\"measurement\","
    "\"device\":{\"identifiers\":[\"greensync_atom_s3_001\"],\"name\":\"GreenSync AtomS3 001\","
    "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\"}}";

  bool ok = client.publish(moistureConfigTopic, moistureConfig, true);

const char* wateredConfigTopic =
  "homeassistant/binary_sensor/greensync_atom_s3_001/watered/config";

const char* wateredConfig =
  "{\"name\":\"Pump Active\","
  "\"unique_id\":\"greensync_atom_s3_001_pump_active\","
  "\"state_topic\":\"greensync/atom-s3-001/state\","
  "\"value_template\":\"{{ 'ON' if value_json.watered else 'OFF' }}\","
  "\"payload_on\":\"ON\","
  "\"payload_off\":\"OFF\","
  "\"device\":{\"identifiers\":[\"greensync_atom_s3_001\"],"
  "\"name\":\"GreenSync AtomS3 001\","
  "\"manufacturer\":\"GreenSync\","
  "\"model\":\"ATOMS3 Lite Watering Unit\"}}";

client.publish(wateredConfigTopic, wateredConfig, true);

const char* rssiConfigTopic =
  "homeassistant/sensor/greensync_atom_s3_001/rssi/config";

const char* rssiConfig =
  "{\"name\":\"WiFi RSSI\","
  "\"unique_id\":\"greensync_atom_s3_001_rssi\","
  "\"state_topic\":\"greensync/atom-s3-001/state\","
  "\"value_template\":\"{{ value_json.rssi }}\","
  "\"unit_of_measurement\":\"dBm\","
  "\"device_class\":\"signal_strength\","
  "\"state_class\":\"measurement\","
  "\"device\":{\"identifiers\":[\"greensync_atom_s3_001\"],"
  "\"name\":\"GreenSync AtomS3 001\","
  "\"manufacturer\":\"GreenSync\","
  "\"model\":\"ATOMS3 Lite Watering Unit\"}}";

client.publish(rssiConfigTopic, rssiConfig, true);

const char* thresholdConfigTopic =
  "homeassistant/number/greensync_atom_s3_001/watering_threshold/config";

const char* thresholdConfig =
  "{\"name\":\"Watering Threshold\","
  "\"unique_id\":\"greensync_atom_s3_001_watering_threshold\","
  "\"command_topic\":\"greensync/atom-s3-001/threshold/set\","
  "\"state_topic\":\"greensync/atom-s3-001/threshold/state\","
  "\"value_template\":\"{{ value_json.wateringThreshold }}\","
  "\"unit_of_measurement\":\"%\","
  "\"min\":0,"
  "\"max\":100,"
  "\"step\":1,"
  "\"mode\":\"box\","
  "\"device\":{\"identifiers\":[\"greensync_atom_s3_001\"],"
  "\"name\":\"GreenSync AtomS3 001\","
  "\"manufacturer\":\"GreenSync\","
  "\"model\":\"ATOMS3 Lite Watering Unit\"}}";

client.publish(thresholdConfigTopic, thresholdConfig, true);

  Serial.print("Discovery publish result=");
  Serial.println(ok ? "OK" : "NG");
  publishThresholdState();
}

bool MQTTService::publishState(int raw, int moisturePercent, int rssi, bool watered) {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"deviceId\":\"%s\",\"raw\":%d,\"moisture\":%d,\"rssi\":%d,\"watered\":%s,\"wateringThreshold\":%d}",
    DeviceId, raw, moisturePercent, rssi, watered ? "true" : "false",
    wateringSettings != nullptr ? wateringSettings->wateringThresholdPercent() : 0);

  Serial.print("Publish: ");
  Serial.println(payload);
  return client.publish(StateTopic, payload, true);
}

bool MQTTService::publishSettings() {
  if (wateringSettings == nullptr) {
    return false;
  }

  publishThresholdState();
  return true;
}
