#include "PumpEvaluationService.h"

#include "MQTTService.h"
#include "PumpController.h"

namespace {
constexpr size_t MaxCommandBytes = 16;
constexpr int ProfileOffMs = 1500;
constexpr int ProfilePulses = 10;

bool resolveProfile(const String& name, int& pulseOnMs) {
  if (name == "TEST_A") pulseOnMs = 250;
  else if (name == "TEST_B") pulseOnMs = 500;
  else if (name == "TEST_C") pulseOnMs = 750;
  else if (name == "TEST_D") pulseOnMs = 1000;
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
  int ignoredOnMs = 0;
  if (command != "CANCEL" && !resolveProfile(command, ignoredOnMs)) {
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
                                 : "ポンプ評価を中止しました");
    } else {
      start(command, controllerIdle, emergencyStopActive, otaUnavailable,
            calibrationUnavailable);
    }
  }

  if (state_ == State::Idle) return;
  if (emergencyStopActive || otaUnavailable || calibrationUnavailable) {
    stop("ABORTED", "安全条件によりポンプ評価を中断しました");
    return;
  }

  const unsigned long elapsed = millis() - phaseStartedAtMs_;
  if (state_ == State::PulseOn &&
      elapsed >= static_cast<unsigned long>(pulseOnMs_)) {
    pump_->off();
    accumulatedOnMs_ += pulseOnMs_;
    ++completedPulses_;
    if (completedPulses_ >= targetPulses_) {
      stop("COMPLETED", "評価パルスが完了しました。散水量を記録してください");
      return;
    }
    state_ = State::PulseOff;
    publish("PULSE_OFF", "休止中です");
    phaseStartedAtMs_ = millis();
  } else if (state_ == State::PulseOff &&
             elapsed >= static_cast<unsigned long>(pulseOffMs_)) {
    state_ = State::PulseOn;
    publish("PULSE_ON", "評価パルスを出力中です");
    pump_->on();
    phaseStartedAtMs_ = millis();
  }
}

bool PumpEvaluationService::isActive() const {
  return state_ != State::Idle;
}

bool PumpEvaluationService::isPumpOn() const {
  return state_ == State::PulseOn;
}

bool PumpEvaluationService::hasPendingCommand() const {
  return !queuedCommand_.isEmpty();
}

bool PumpEvaluationService::publishCurrentState() {
  if (state_ == State::PulseOn) {
    return publish("PULSE_ON", "評価パルスを出力中です");
  }
  if (state_ == State::PulseOff) return publish("PULSE_OFF", "休止中です");
  return publish("IDLE",
                 "評価用FW：自動散水は無効です。条件を選び1回だけ実行してください");
}

void PumpEvaluationService::start(const String& profileName,
                                  bool controllerIdle,
                                  bool emergencyStopActive,
                                  bool otaUnavailable,
                                  bool calibrationUnavailable) {
  int onMs = 0;
  if (!resolveProfile(profileName, onMs) || !controllerIdle ||
      emergencyStopActive || otaUnavailable || calibrationUnavailable ||
      pump_ == nullptr || mqtt_ == nullptr) {
    publish("REJECTED", "安全条件を満たさないため評価を開始できません");
    return;
  }

  profileName_ = profileName == "TEST_A" ? "A" :
                 profileName == "TEST_B" ? "B" :
                 profileName == "TEST_C" ? "C" : "D";
  pulseOnMs_ = onMs;
  pulseOffMs_ = ProfileOffMs;
  targetPulses_ = ProfilePulses;
  completedPulses_ = 0;
  accumulatedOnMs_ = 0;
  state_ = State::PulseOn;
  publish("PULSE_ON", "評価パルスを開始しました");
  pump_->on();
  phaseStartedAtMs_ = millis();
}

void PumpEvaluationService::stop(const char* state, const char* message) {
  if (pump_ != nullptr) pump_->off();
  state_ = State::Idle;
  publish(state, message);
}

bool PumpEvaluationService::publish(const char* state, const char* message) {
  if (mqtt_ == nullptr) return false;
  return mqtt_->publishPumpEvaluationState(
      state, profileName_, pulseOnMs_, pulseOffMs_, targetPulses_,
      completedPulses_, accumulatedOnMs_, message);
}
