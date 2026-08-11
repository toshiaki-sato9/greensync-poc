#include "PumpEvaluationService.h"

#include "Config.h"
#include "MQTTService.h"
#include "PumpController.h"

namespace {
constexpr size_t MaxCommandBytes = 16;

bool resolveProfile(const String& name, int& dutyPercent) {
  if (name == "TEST_25") dutyPercent = 25;
  else if (name == "TEST_30") dutyPercent = 30;
  else if (name == "TEST_35") dutyPercent = 35;
  else if (name == "TEST_40") dutyPercent = 40;
  else if (name == "TEST_45") dutyPercent = 45;
  else if (name == "TEST_50") dutyPercent = 50;
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
  int ignoredDuty = 0;
  if (command != "CANCEL" && !resolveProfile(command, ignoredDuty)) {
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
      static_cast<unsigned long>(Config::PumpEvaluationDurationMs)) {
    stop("COMPLETED", "30秒間のPWM評価が完了しました。重量と吐水状態を記録してください");
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
    return publish("RUNNING", "20kHz PWMを30秒間出力中です");
  }
  return publish("IDLE",
                 "評価用FW：自動散水は無効です。PWM条件を1つ選んでください");
}

void PumpEvaluationService::start(const String& profileName,
                                  bool controllerIdle,
                                  bool emergencyStopActive,
                                  bool otaUnavailable,
                                  bool calibrationUnavailable) {
  int duty = 0;
  if (!resolveProfile(profileName, duty) || !controllerIdle ||
      emergencyStopActive || otaUnavailable || calibrationUnavailable ||
      pump_ == nullptr || mqtt_ == nullptr) {
    publish("REJECTED", "安全条件を満たさないためPWM評価を開始できません");
    return;
  }

  dutyPercent_ = duty;
  profileName_ = duty == 25 ? "PWM_25" : duty == 30 ? "PWM_30" :
                 duty == 35 ? "PWM_35" : duty == 40 ? "PWM_40" :
                 duty == 45 ? "PWM_45" : "PWM_50";
  state_ = State::Running;
  publish("RUNNING", "20kHz PWMを30秒間出力中です");
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
      Config::PumpEvaluationDurationMs, message);
}
