#include <Arduino.h>
#include <M5Unified.h>

#include "Config.h"
#include "MoistureSensor.h"
#include "PumpController.h"
#include "WateringSettings.h"
#include "WiFiService.h"
#include "MQTTService.h"

MoistureSensor moistureSensor;
PumpController pump;
WateringSettings settings;
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

  settings.begin();
  wifi.begin();
  mqtt.begin(&settings);
}

void loop() {
  M5.update();
  mqtt.loop();

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
  Serial.print("%, threshold=");
  Serial.print(settings.wateringThresholdPercent());
  Serial.println("%");

  if (!emergencyStop && percent < settings.wateringThresholdPercent()) {
    Serial.println("Soil is dry. Watering...");
    pump.waterForMs(Config::WateringDurationMs);
    watered = true;
    Serial.println("Watering done.");
  }

  mqtt.publishState(raw, percent, wifi.rssi(), watered);
  mqtt.publishSettings();

  Serial.println("--------------------");
  delay(10000);
}
