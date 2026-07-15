#include "MQTTService.h"
#include "Secrets.h"
#include "WateringSettings.h"
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

namespace {
WiFiClient wifiClient;
PubSubClient client(wifiClient);
WateringSettings* wateringSettings = nullptr;

char deviceId[48];
char deviceName[64];
char objectId[48];
char stateTopic[96];
char thresholdStateTopic[96];
char thresholdSetTopic[96];
bool identityInitialized = false;

void initializeIdentity() {
  if (identityInitialized) {
    return;
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);

  char suffix[13];
  snprintf(suffix, sizeof(suffix), "%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  snprintf(deviceId, sizeof(deviceId), "greensync-atom-s3-%s", suffix);
  snprintf(deviceName, sizeof(deviceName), "GreenSync AtomS3 %s", suffix);
  snprintf(objectId, sizeof(objectId), "greensync_atom_s3_%s", suffix);
  snprintf(stateTopic, sizeof(stateTopic), "greensync/%s/state", deviceId);
  snprintf(
      thresholdStateTopic,
      sizeof(thresholdStateTopic),
      "greensync/%s/threshold/state",
      deviceId
  );
  snprintf(
      thresholdSetTopic,
      sizeof(thresholdSetTopic),
      "greensync/%s/threshold/set",
      deviceId
  );

  identityInitialized = true;

  Serial.print("MQTT identity: ");
  Serial.println(deviceId);
}

void buildDiscoveryTopic(
    char* buffer,
    size_t bufferSize,
    const char* component,
    const char* entityId
) {
  snprintf(
      buffer,
      bufferSize,
      "homeassistant/%s/%s/%s/config",
      component,
      objectId,
      entityId
  );
}

void publishThresholdState() {
  if (wateringSettings == nullptr) {
    return;
  }

  char payload[64];
  snprintf(payload, sizeof(payload), "{\"wateringThreshold\":%d}",
           wateringSettings->wateringThresholdPercent());
  client.publish(thresholdStateTopic, payload, true);
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
  initializeIdentity();
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
    if (client.connect(deviceId)) {
      Serial.println("connected");
      client.subscribe(thresholdSetTopic);
      publishDiscovery();
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void MQTTService::publishDiscovery() {
  initializeIdentity();

  Serial.println(">>> publishDiscovery called");
  Serial.print("MQTT connected=");
  Serial.println(client.connected());

  char configTopic[128];
  char payload[768];
  bool ok = true;

  buildDiscoveryTopic(configTopic, sizeof(configTopic), "sensor", "moisture");
  snprintf(
      payload,
      sizeof(payload),
      "{\"name\":\"Soil Moisture\",\"unique_id\":\"%s_moisture\","
      "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.moisture }}\","
      "\"unit_of_measurement\":\"%%\",\"device_class\":\"moisture\",\"state_class\":\"measurement\","
      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\",\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      objectId,
      stateTopic,
      objectId,
      deviceName
  );
  ok = client.publish(configTopic, payload, true) && ok;

  buildDiscoveryTopic(
      configTopic,
      sizeof(configTopic),
      "binary_sensor",
      "watered"
  );
  snprintf(
      payload,
      sizeof(payload),
      "{\"name\":\"Pump Active\","
      "\"unique_id\":\"%s_pump_active\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"{{ 'ON' if value_json.watered else 'OFF' }}\","
      "\"payload_on\":\"ON\","
      "\"payload_off\":\"OFF\","
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      objectId,
      stateTopic,
      objectId,
      deviceName
  );
  ok = client.publish(configTopic, payload, true) && ok;

  buildDiscoveryTopic(configTopic, sizeof(configTopic), "sensor", "rssi");
  snprintf(
      payload,
      sizeof(payload),
      "{\"name\":\"WiFi RSSI\","
      "\"unique_id\":\"%s_rssi\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"{{ value_json.rssi }}\","
      "\"unit_of_measurement\":\"dBm\","
      "\"device_class\":\"signal_strength\","
      "\"state_class\":\"measurement\","
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      objectId,
      stateTopic,
      objectId,
      deviceName
  );
  ok = client.publish(configTopic, payload, true) && ok;

  buildDiscoveryTopic(
      configTopic,
      sizeof(configTopic),
      "number",
      "watering_threshold"
  );
  snprintf(
      payload,
      sizeof(payload),
      "{\"name\":\"Watering Threshold\","
      "\"unique_id\":\"%s_watering_threshold\","
      "\"command_topic\":\"%s\","
      "\"state_topic\":\"%s\","
      "\"value_template\":\"{{ value_json.wateringThreshold }}\","
      "\"unit_of_measurement\":\"%%\","
      "\"min\":0,"
      "\"max\":100,"
      "\"step\":1,"
      "\"mode\":\"box\","
      "\"device\":{\"identifiers\":[\"%s\"],"
      "\"name\":\"%s\","
      "\"manufacturer\":\"GreenSync\","
      "\"model\":\"ATOMS3 Lite Watering Unit\"}}",
      objectId,
      thresholdSetTopic,
      thresholdStateTopic,
      objectId,
      deviceName
  );
  ok = client.publish(configTopic, payload, true) && ok;

  Serial.print("Discovery publish result=");
  Serial.println(ok ? "OK" : "NG");
  publishThresholdState();
}

bool MQTTService::publishState(int raw, int moisturePercent, int rssi, bool watered) {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"deviceId\":\"%s\",\"raw\":%d,\"moisture\":%d,\"rssi\":%d,\"watered\":%s,\"wateringThreshold\":%d}",
    deviceId, raw, moisturePercent, rssi, watered ? "true" : "false",
    wateringSettings != nullptr ? wateringSettings->wateringThresholdPercent() : 0);

  Serial.print("Publish: ");
  Serial.println(payload);
  return client.publish(stateTopic, payload, true);
}

bool MQTTService::publishSettings() {
  if (wateringSettings == nullptr) {
    return false;
  }

  publishThresholdState();
  return true;
}
