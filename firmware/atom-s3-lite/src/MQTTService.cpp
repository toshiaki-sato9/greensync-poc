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