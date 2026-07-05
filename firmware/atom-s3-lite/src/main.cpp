#include <Arduino.h>
#include <M5Unified.h>
#include "Config.h"
#include "MoistureSensor.h"
#include "PumpController.h"

MoistureSensor moistureSensor;
PumpController pump;

void setup() {
  Serial.begin(115200);
  delay(2000);

  auto cfg = M5.config();
  M5.begin(cfg);

  pump.begin();

  Serial.println();
  Serial.println("===== GreenSync Firmware v0.1.0 =====");
}

void loop() {
  int raw = moistureSensor.readRaw();
  int percent = moistureSensor.readPercent();

  Serial.print("raw=");
  Serial.print(raw);
  Serial.print(", moisture=");
  Serial.print(percent);
  Serial.println("%");

  if (percent < Config::WateringThresholdPercent) {
    Serial.println("Soil is dry. Watering...");
    pump.waterForMs(Config::WateringDurationMs);
    Serial.println("Watering done.");
  } else {
    Serial.println("Soil moisture is enough. No watering.");
  }

  Serial.println("--------------------");
  delay(10000);
}
