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
  if (command != "START" && command != "CAPTURE" && command != "CANCEL") {
    return false;
  }
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
    } else if (command == "CAPTURE") {
      capturePoint();
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
      return publish("AWAITING_DRY",
                     "乾燥値が安定したら「② 表示された基準値を記録」を押してください");
    case State::AwaitingWet:
      return publish(
          "AWAITING_WET",
          "十分に湿らせた基準土へ移し、値が安定したら「② 表示された基準値を記録」を押してください");
    case State::Idle:
      return publish("IDLE", "待機中（校正を開始できます）");
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
          "乾燥値が安定したら「② 表示された基準値を記録」を押してください");
}

void CalibrationService::capturePoint() {
  if (sensor_ == nullptr || settings_ == nullptr) {
    cancel("Calibration service is unavailable");
    return;
  }
  if (state_ == State::Idle) {
    publish("FAILED", "先に「① 乾燥土で校正開始」を押してください");
    return;
  }
  const int sampleRaw = sensor_->readRaw();
  if (state_ == State::AwaitingDry) {
    pendingDryRaw_ = sampleRaw;
    state_ = State::AwaitingWet;
    publish("AWAITING_WET",
            "乾燥値を取得しました。十分に湿らせた基準土へ移し、値が安定したら「② 表示された基準値を記録」を押してください",
            sampleRaw);
    return;
  }

  const int wetRaw = sampleRaw;
  if (pendingDryRaw_ <= wetRaw ||
      pendingDryRaw_ - wetRaw < Config::CalibrationMinimumSpanRaw) {
    state_ = State::Idle;
    publish("FAILED", "校正失敗：乾燥値と湿潤値の差が不足しています（以前の値を維持）",
            wetRaw);
    pendingDryRaw_ = 0;
    return;
  }
  if (!settings_->setMoistureCalibration(pendingDryRaw_, wetRaw)) {
    state_ = State::Idle;
    publish("FAILED", "校正値を保存できませんでした（以前の値を維持）",
            wetRaw);
    pendingDryRaw_ = 0;
    return;
  }
  state_ = State::Idle;
  publish("SUCCEEDED", "校正完了：新しい乾燥値と湿潤値を保存しました", wetRaw);
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
