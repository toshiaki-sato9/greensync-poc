#include "PumpEvaluationService.h"

#include "Config.h"
#include "MQTTService.h"
#include "PumpController.h"

namespace {
constexpr size_t MaxCommandBytes = 16;

bool resolveProfile(const String& name, int& durationMs) {
  if (name == "TEST_3S") durationMs = 3000;
  else if (name == "TEST_5S") durationMs = 5000;
  else if (name == "TEST_10S") durationMs = 10000;
  else if (name == "TEST_15S") durationMs = 15000;
  else return false;
  return true;
}
}

void PumpEvaluationService::begin(PumpController* pump,
                                  MQTTService* mqttService) {
  pump_ = pump;
  mqtt_ = mqttService;
}

bool PumpEvaluationService::queueCommand(const byte* payload,
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
  int ignoredDuration = 0;
  if (command != "CANCEL" && !resolveProfile(command, ignoredDuration)) {
    return false;
  }
  if (state_ != State::Idle && command != "CANCEL") return false;
  queuedCommand_ = command;
  return true;
}

void PumpEvaluationService::loop(bool controllerIdle,
                                 bool emergencyStopActive,
                                 bool otaUnavailable,
                                 bool calibrationUnavailable) {
  if (!queuedCommand_.isEmpty()) {
    const String command = queuedCommand_;
    queuedCommand_ = "";
    if (command == "CANCEL") {
      stop(state_ == State::Idle ? "IDLE" : "CANCELLED",
           state_ == State::Idle ? "ポンプ評価は待機中です"
                                 : "PWM評価を中止しました");
    } else {
      start(command, controllerIdle, emergencyStopActive, otaUnavailable,
            calibrationUnavailable);
    }
  }

  if (state_ == State::Idle) return;
  if (emergencyStopActive || otaUnavailable || calibrationUnavailable) {
    stop("ABORTED", "安全条件によりPWM評価を中断しました");
    return;
  }
  if (millis() - startedAtMs_ >=
      static_cast<unsigned long>(durationMs_)) {
    stop("COMPLETED", "31% PWM時間評価が完了しました。重量を記録してください");
  }
}

bool PumpEvaluationService::isActive() const {
  return state_ != State::Idle;
}

bool PumpEvaluationService::isPumpOn() const {
  return state_ == State::Running;
}

bool PumpEvaluationService::hasPendingCommand() const {
  return !queuedCommand_.isEmpty();
}

bool PumpEvaluationService::publishCurrentState() {
  if (state_ == State::Running) {
    return publish("RUNNING", "20kHz・31% PWMで時間評価中です");
  }
  return publish("IDLE",
                 "評価用FW：自動散水は無効です。PWM条件を1つ選んでください");
}

void PumpEvaluationService::start(const String& profileName,
                                  bool controllerIdle,
                                  bool emergencyStopActive,
                                  bool otaUnavailable,
                                  bool calibrationUnavailable) {
  int duration = 0;
  if (!resolveProfile(profileName, duration) ||
      duration > Config::PumpEvaluationMaxDurationMs || !controllerIdle ||
      emergencyStopActive || otaUnavailable || calibrationUnavailable ||
      pump_ == nullptr || mqtt_ == nullptr) {
    publish("REJECTED", "安全条件を満たさないためPWM評価を開始できません");
    return;
  }

  dutyPercent_ = Config::PumpEvaluationDutyPercent;
  durationMs_ = duration;
  profileName_ = duration == 3000 ? "PWM_31_3S" :
                 duration == 5000 ? "PWM_31_5S" :
                 duration == 10000 ? "PWM_31_10S" : "PWM_31_15S";
  state_ = State::Running;
  publish("RUNNING", "20kHz・31% PWMで時間評価中です");
  pump_->setDutyPercent(dutyPercent_);
  startedAtMs_ = millis();
}

void PumpEvaluationService::stop(const char* state, const char* message) {
  if (pump_ != nullptr) pump_->off();
  state_ = State::Idle;
  publish(state, message);
}

bool PumpEvaluationService::publish(const char* state, const char* message) {
  if (mqtt_ == nullptr) return false;
  return mqtt_->publishPumpEvaluationState(
      state, profileName_, dutyPercent_, Config::PumpPwmFrequencyHz,
      durationMs_, message);
}
