#include "PumpController.h"
#include "Config.h"
#include <Arduino.h>

void PumpController::begin() {
  pinMode(Config::PumpPin, OUTPUT);
  off();
}

void PumpController::off() {
  digitalWrite(Config::PumpPin, LOW);
}

void PumpController::on() {
  digitalWrite(Config::PumpPin, HIGH);
}

void PumpController::waterForMs(int durationMs) {
  on();
  delay(durationMs);
  off();
}
