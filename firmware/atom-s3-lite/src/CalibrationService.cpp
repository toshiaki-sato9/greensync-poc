#include "CalibrationService.h"

#include "Config.h"
#include "MQTTService.h"
#include "MoistureSensor.h"
#include "WateringSettings.h"

#include <cstring>

namespace {
constexpr size_t MaxCommandBytes = 16;
}

void CalibrationService::begin(MoistureSensor* sensor,
                               WateringSettings* settings,
                               MQTTService* mqttService) {
  sensor_ = sensor;
  settings_ = settings;
  mqtt_ = mqttService;
}

bool CalibrationService::queueCommand(const byte* payload,
                                      unsigned int length) {
  if (length == 0 || length > MaxCommandBytes || !queuedCommand_.isEmpty()) {
    return false;
  }
  String command;
  command.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) {
    command += static_cast<char>(payload[i]);
  }
  command.trim();
  command.toUpperCase();
  if (command != "START" && command != "CANCEL") return false;
  queuedCommand_ = command;
  return true;
}

void CalibrationService::loop(bool controllerIdle, bool emergencyStopActive,
                              bool otaUnavailable, bool buttonClicked) {
  if (!queuedCommand_.isEmpty()) {
    const String command = queuedCommand_;
    queuedCommand_ = "";
    if (command == "CANCEL") {
      cancel("Calibration cancelled");
    } else if (command == "START") {
      start(controllerIdle, emergencyStopActive, otaUnavailable);
    }
  }

  if (state_ == State::Idle) return;
  if (emergencyStopActive) {
    cancel("Emergency stop activated");
    return;
  }
  if (millis() - startedAtMs_ >=
      static_cast<unsigned long>(Config::CalibrationTimeoutMs)) {
    cancel("Calibration timed out");
    return;
  }
  if (buttonClicked) capturePoint();
}

bool CalibrationService::isActive() const { return state_ != State::Idle; }

bool CalibrationService::hasPendingCommand() const {
  return !queuedCommand_.isEmpty();
}

bool CalibrationService::publishCurrentState() {
  switch (state_) {
    case State::AwaitingDry:
      return publish("AWAITING_DRY", "Waiting for dry reference button press");
    case State::AwaitingWet:
      return publish("AWAITING_WET", "Waiting for wet reference button press");
    case State::Idle:
      return publish("IDLE", "Calibration is idle");
  }
  return false;
}

void CalibrationService::start(bool controllerIdle, bool emergencyStopActive,
                               bool otaUnavailable) {
  if (state_ != State::Idle) {
    publish("FAILED", "Calibration is already active");
    return;
  }
  if (!controllerIdle || emergencyStopActive || otaUnavailable ||
      sensor_ == nullptr || settings_ == nullptr) {
    publish("FAILED", "Device is not ready for calibration");
    return;
  }
  pendingDryRaw_ = 0;
  startedAtMs_ = millis();
  state_ = State::AwaitingDry;
  publish("AWAITING_DRY",
          "Place the sensor in the dry reference, then press the Atom button");
}

void CalibrationService::capturePoint() {
  if (sensor_ == nullptr || settings_ == nullptr) {
    cancel("Calibration service is unavailable");
    return;
  }
  const int sampleRaw = sensor_->readRaw();
  if (state_ == State::AwaitingDry) {
    pendingDryRaw_ = sampleRaw;
    state_ = State::AwaitingWet;
    publish("AWAITING_WET",
            "Dry point captured; place the sensor in saturated reference soil, then press the Atom button",
            sampleRaw);
    return;
  }

  const int wetRaw = sampleRaw;
  if (pendingDryRaw_ <= wetRaw ||
      pendingDryRaw_ - wetRaw < Config::CalibrationMinimumSpanRaw) {
    state_ = State::Idle;
    publish("FAILED", "Calibration span is invalid; previous values were kept",
            wetRaw);
    pendingDryRaw_ = 0;
    return;
  }
  if (!settings_->setMoistureCalibration(pendingDryRaw_, wetRaw)) {
    state_ = State::Idle;
    publish("FAILED", "Could not save calibration; previous values were kept",
            wetRaw);
    pendingDryRaw_ = 0;
    return;
  }
  state_ = State::Idle;
  publish("SUCCEEDED", "Moisture calibration saved", wetRaw);
  pendingDryRaw_ = 0;
}

void CalibrationService::cancel(const char* message) {
  if (state_ == State::Idle) {
    publish("IDLE", message);
    return;
  }
  state_ = State::Idle;
  publish("CANCELLED", message);
  pendingDryRaw_ = 0;
}

bool CalibrationService::publish(const char* state, const char* message,
                                 int sampleRaw) {
  if (mqtt_ == nullptr || settings_ == nullptr) return false;
  return mqtt_->publishCalibrationState(
      state, settings_->moistureDryRaw(), settings_->moistureWetRaw(),
      pendingDryRaw_, sampleRaw, message);
}
