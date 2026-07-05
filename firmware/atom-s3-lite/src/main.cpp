#include <Arduino.h>
#include <M5Unified.h>

#include "Config.h"
#include "MoistureSensor.h"
#include "PumpController.h"
#include "WiFiService.h"
#include "MQTTService.h"

MoistureSensor moistureSensor;
PumpController pump;
WiFiService wifi;
MQTTService mqtt;

bool emergencyStop = false;

void setup() {
  Serial.begin(115200);
  delay(2000);

  auto cfg = M5.config();
  M5.begin(cfg);

  pump.begin();
  pump.off();

  Serial.println();
  Serial.println("===== GreenSync Firmware v0.2.0 MQTT =====");

  wifi.begin();
  mqtt.begin();

  delay(1000);
  mqtt.publishDiscovery();
}

void loop() {
  M5.update();
  mqtt.loop();
static bool discoveryDone = false;

if (!discoveryDone) {
    mqtt.publishDiscovery();
    discoveryDone = true;
}
  if (M5.BtnA.pressedFor(1500)) {
    emergencyStop = true;
    pump.off();
    Serial.println("EMERGENCY STOP: Pump forced OFF.");
  }

  int raw = moistureSensor.readRaw();
  int percent = moistureSensor.readPercent();
  bool watered = false;

  Serial.print("raw=");
  Serial.print(raw);
  Serial.print(", moisture=");
  Serial.print(percent);
  Serial.println("%");

  if (!emergencyStop && percent < Config::WateringThresholdPercent) {
    Serial.println("Soil is dry. Watering...");
    pump.waterForMs(Config::WateringDurationMs);
    watered = true;
    Serial.println("Watering done.");
  }

  mqtt.publishState(raw, percent, wifi.rssi(), watered);

  Serial.println("--------------------");
  delay(10000);
}