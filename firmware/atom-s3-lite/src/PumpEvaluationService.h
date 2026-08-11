#pragma once

#include <Arduino.h>

class MQTTService;
class PumpController;
class WateringSettings;

class PumpEvaluationService {
public:
  void begin(PumpController* pump, MQTTService* mqttService,
             WateringSettings* settings);
  bool queueCommand(const byte* payload, unsigned int length);
  bool setDraftDutyPercent(int value);
  bool setDraftDurationSeconds(int value);
  bool setMeasuredVolumeMl(int value);
  void loop(bool controllerIdle, bool emergencyStopActive, bool otaUnavailable,
            bool moistureCalibrationUnavailable);
  bool isActive() const;
  bool isPumpOn() const;
  bool hasPendingCommand() const;
  bool publishCurrentState();

private:
  enum class State { Idle, Active, TestRunning };
  void start(bool controllerIdle, bool emergencyStopActive,
             bool otaUnavailable, bool moistureCalibrationUnavailable);
  void startTest();
  void save();
  void stop(const char* state, const char* message);
  bool publish(const char* state, const char* message);

  PumpController* pump_ = nullptr;
  MQTTService* mqtt_ = nullptr;
  WateringSettings* settings_ = nullptr;
  String queuedCommand_;
  State state_ = State::Idle;
  unsigned long startedAtMs_ = 0;
  int dutyPercent_ = 50;
  int durationSeconds_ = 30;
  int measuredVolumeMl_ = 0;
};
