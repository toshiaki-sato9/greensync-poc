#include "PumpEvaluationService.h"

#include "Config.h"
#include "MQTTService.h"
#include "PumpController.h"

namespace {
constexpr size_t MaxCommandBytes = 16;

bool resolveProfile(const String& name, int& dutyPercent, int& durationMs) {
  durationMs = Config::PumpEvaluationMaxDurationMs;
  if (name == "TEST_41") dutyPercent = 41;
  else if (name == "TEST_42") dutyPercent = 42;
  else if (name == "TEST_43") dutyPercent = 43;
  else if (name == "TEST_44") dutyPercent = 44;
  else if (name == "TEST_45") dutyPercent = 45;
  else if (name == "TEST_46") dutyPercent = 46;
  else if (name == "TEST_47") dutyPercent = 47;
  else if (name == "TEST_48") dutyPercent = 48;
  else if (name == "TEST_49") dutyPercent = 49;
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
  int ignoredDuration = 0;
  if (command != "CANCEL" &&
      !resolveProfile(command, ignoredDuty, ignoredDuration)) {
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
    stop("COMPLETED", "個体別の始動境界評価が完了しました。吐水有無を記録してください");
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
    return publish("RUNNING", "20kHz PWMで個体別の始動境界を評価中です");
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
  int duration = 0;
  if (!resolveProfile(profileName, duty, duration) || !controllerIdle ||
      emergencyStopActive || otaUnavailable || calibrationUnavailable ||
      pump_ == nullptr || mqtt_ == nullptr) {
    publish("REJECTED", "安全条件を満たさないためPWM評価を開始できません");
    return;
  }

  dutyPercent_ = duty;
  durationMs_ = duration;
  profileName_ = duty == 41 ? "PWM_41" : duty == 42 ? "PWM_42" :
                 duty == 43 ? "PWM_43" : duty == 44 ? "PWM_44" :
                 duty == 45 ? "PWM_45" : duty == 46 ? "PWM_46" :
                 duty == 47 ? "PWM_47" : duty == 48 ? "PWM_48" : "PWM_49";
  state_ = State::Running;
  publish("RUNNING", "20kHz PWMで個体別の始動境界を評価中です");
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
