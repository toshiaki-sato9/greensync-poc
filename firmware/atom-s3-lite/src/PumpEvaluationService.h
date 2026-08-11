#pragma once

#include <Arduino.h>

class MQTTService;
class PumpController;

class PumpEvaluationService {
public:
  void begin(PumpController* pump, MQTTService* mqttService);
  bool queueCommand(const byte* payload, unsigned int length);
  void loop(bool controllerIdle, bool emergencyStopActive, bool otaUnavailable,
            bool calibrationUnavailable);
  bool isActive() const;
  bool isPumpOn() const;
  bool hasPendingCommand() const;
  bool publishCurrentState();

private:
  enum class State { Idle, Running };

  void start(const String& profileName, bool controllerIdle,
             bool emergencyStopActive, bool otaUnavailable,
             bool calibrationUnavailable);
  void stop(const char* state, const char* message);
  bool publish(const char* state, const char* message);

  PumpController* pump_ = nullptr;
  MQTTService* mqtt_ = nullptr;
  String queuedCommand_;
  State state_ = State::Idle;
  const char* profileName_ = "";
  unsigned long startedAtMs_ = 0;
  int dutyPercent_ = 0;
  int durationMs_ = 0;
};
