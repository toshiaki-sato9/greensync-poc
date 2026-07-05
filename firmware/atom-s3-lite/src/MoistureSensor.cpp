#include "MoistureSensor.h"
#include "Config.h"
#include <Arduino.h>

int MoistureSensor::readRaw() const {
  return analogRead(Config::MoisturePin);
}

int MoistureSensor::readPercent() const {
  int raw = readRaw();
  int percent = map(raw, Config::DryRaw, Config::WetRaw, 0, 100);
  return constrain(percent, 0, 100);
}
