#include "MQTTService.h"
#include "Secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

namespace {
WiFiClient wifiClient;
PubSubClient client(wifiClient);

const char* DeviceId = "greensync-atom-s3-001";
const char* StateTopic = "greensync/atom-s3-001/state";
}

void MQTTService::begin() {
  client.setServer(MQTT_HOST, MQTT_PORT);
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

  Serial.print("Discovery publish result=");
  Serial.println(ok ? "OK" : "NG");
}

bool MQTTService::publishState(int raw, int moisturePercent, int rssi, bool watered) {
  char payload[256];
  snprintf(payload, sizeof(payload),
    "{\"deviceId\":\"%s\",\"raw\":%d,\"moisture\":%d,\"rssi\":%d,\"watered\":%s}",
    DeviceId, raw, moisturePercent, rssi, watered ? "true" : "false");

  Serial.print("Publish: ");
  Serial.println(payload);
  return client.publish(StateTopic, payload, true);
}