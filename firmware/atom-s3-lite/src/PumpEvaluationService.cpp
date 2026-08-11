#include "PumpEvaluationService.h"

#include "MQTTService.h"
#include "PumpController.h"
#include "WateringSettings.h"

namespace { constexpr size_t MaxCommandBytes = 16; }

void PumpEvaluationService::begin(PumpController* pump,
                                  MQTTService* mqttService,
                                  WateringSettings* settings) {
  pump_ = pump;
  mqtt_ = mqttService;
  settings_ = settings;
}

bool PumpEvaluationService::queueCommand(const byte* payload,
                                         unsigned int length) {
  if (length == 0 || length > MaxCommandBytes || !queuedCommand_.isEmpty()) {
    return false;
  }
  String command;
  for (unsigned int i = 0; i < length; ++i) command += char(payload[i]);
  command.trim();
  command.toUpperCase();
  if (command != "START" && command != "TEST" && command != "SAVE" &&
      command != "CANCEL") return false;
  if (state_ == State::TestRunning && command != "CANCEL") return false;
  queuedCommand_ = command;
  return true;
}

bool PumpEvaluationService::setDraftDutyPercent(int value) {
  if (state_ != State::Active || value < 1 || value > 100) return false;
  dutyPercent_ = value;
  return publish("ACTIVE", "Dutyを設定しました。テストを実行してください");
}

bool PumpEvaluationService::setDraftDurationSeconds(int value) {
  if (state_ != State::Active || value < 1 || value > 60) return false;
  durationSeconds_ = value;
  return publish("ACTIVE", "評価時間を設定しました");
}

bool PumpEvaluationService::setMeasuredVolumeMl(int value) {
  if (state_ != State::Active || value < 1 || value > 1000) return false;
  measuredVolumeMl_ = value;
  return publish("ACTIVE", "実測水量を設定しました。保存できます");
}

void PumpEvaluationService::loop(bool controllerIdle,
                                 bool emergencyStopActive,
                                 bool otaUnavailable,
                                 bool moistureCalibrationUnavailable) {
  if (!queuedCommand_.isEmpty()) {
    const String command = queuedCommand_;
    queuedCommand_ = "";
    if (command == "START") {
      start(controllerIdle, emergencyStopActive, otaUnavailable,
            moistureCalibrationUnavailable);
    } else if (command == "TEST") startTest();
    else if (command == "SAVE") save();
    else stop(state_ == State::Idle ? "IDLE" : "CANCELLED",
              "ポンプ校正を中止しました。以前の値は維持されます");
  }

  if (state_ == State::Idle) return;
  if (emergencyStopActive || otaUnavailable ||
      moistureCalibrationUnavailable) {
    stop("ABORTED", "安全条件によりポンプ校正を中断しました");
    return;
  }
  if (state_ == State::TestRunning &&
      millis() - startedAtMs_ >=
          static_cast<unsigned long>(durationSeconds_) * 1000UL) {
    pump_->off();
    state_ = State::Active;
    publish("AWAITING_MEASUREMENT",
            "テスト完了。実測水量を入力して保存してください");
  }
}

bool PumpEvaluationService::isActive() const { return state_ != State::Idle; }
bool PumpEvaluationService::isPumpOn() const {
  return state_ == State::TestRunning;
}
bool PumpEvaluationService::hasPendingCommand() const {
  return !queuedCommand_.isEmpty();
}

bool PumpEvaluationService::publishCurrentState() {
  if (state_ == State::TestRunning) return publish("TEST_RUNNING", "テスト散水中です");
  if (state_ == State::Active) return publish("ACTIVE", "校正値を設定してください");
  return publish("IDLE", settings_ != nullptr && settings_->hasPumpCalibration()
                             ? "ポンプ校正済み"
                             : "未校正：自動散水は禁止されています");
}

void PumpEvaluationService::start(bool controllerIdle,
                                  bool emergencyStopActive,
                                  bool otaUnavailable,
                                  bool moistureCalibrationUnavailable) {
  if (state_ != State::Idle || !controllerIdle || emergencyStopActive ||
      otaUnavailable || moistureCalibrationUnavailable || pump_ == nullptr ||
      settings_ == nullptr) {
    publish("REJECTED", "安全条件を満たさないため校正を開始できません");
    return;
  }
  dutyPercent_ = settings_->hasPumpCalibration()
                     ? settings_->pumpDutyPercent()
                     : 50;
  durationSeconds_ = settings_->hasPumpCalibration()
                         ? settings_->pumpReferenceDurationMs() / 1000
                         : 30;
  measuredVolumeMl_ = 0;
  state_ = State::Active;
  publish("ACTIVE", "Dutyと評価時間を設定し、テストを実行してください");
}

void PumpEvaluationService::startTest() {
  if (state_ != State::Active || pump_ == nullptr) {
    publish("REJECTED", "先にポンプ校正を開始してください");
    return;
  }
  measuredVolumeMl_ = 0;
  state_ = State::TestRunning;
  publish("TEST_RUNNING", "テスト散水中です");
  pump_->setDutyPercent(dutyPercent_);
  startedAtMs_ = millis();
}

void PumpEvaluationService::save() {
  if (state_ != State::Active || settings_ == nullptr ||
      measuredVolumeMl_ <= 0) {
    publish("REJECTED", "実測水量を入力してから保存してください");
    return;
  }
  const int flowMilliMlPerSecond =
      measuredVolumeMl_ * 1000 / durationSeconds_;
  if (!settings_->setPumpCalibration(
          dutyPercent_, flowMilliMlPerSecond, durationSeconds_ * 1000,
          measuredVolumeMl_)) {
    publish("FAILED", "校正値を保存できませんでした。以前の値を維持します");
    return;
  }
  state_ = State::Idle;
  publish("SUCCEEDED", "端末別のポンプ校正値を保存しました");
}

void PumpEvaluationService::stop(const char* state, const char* message) {
  if (pump_ != nullptr) pump_->off();
  state_ = State::Idle;
  publish(state, message);
}

bool PumpEvaluationService::publish(const char* state, const char* message) {
  if (mqtt_ == nullptr) return false;
  return mqtt_->publishPumpEvaluationState(
      state, "PUMP_CALIBRATION", dutyPercent_, 20000,
      durationSeconds_ * 1000, measuredVolumeMl_,
      settings_ != nullptr && settings_->hasPumpCalibration(),
      settings_ != nullptr ? settings_->pumpFlowMilliMlPerSecond() : 0,
      message);
}
