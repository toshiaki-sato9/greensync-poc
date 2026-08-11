#include "PumpController.h"
#include "Config.h"
#include <Arduino.h>

void PumpController::begin() {
  ledcSetup(Config::PumpPwmChannel, Config::PumpPwmFrequencyHz,
            Config::PumpPwmResolutionBits);
  ledcAttachPin(Config::PumpPin, Config::PumpPwmChannel);
  off();
}

void PumpController::off() {
  setDutyPercent(0);
}

void PumpController::on() {
  setDutyPercent(100);
}

void PumpController::setDutyPercent(int dutyPercent) {
  dutyPercent_ = constrain(dutyPercent, 0, 100);
  const uint32_t maximumDuty =
      (1U << Config::PumpPwmResolutionBits) - 1U;
  const uint32_t duty =
      maximumDuty * static_cast<uint32_t>(dutyPercent_) / 100U;
  ledcWrite(Config::PumpPwmChannel, duty);
}

int PumpController::dutyPercent() const { return dutyPercent_; }

void PumpController::waterForMs(int durationMs) {
  on();
  delay(durationMs);
  off();
}
