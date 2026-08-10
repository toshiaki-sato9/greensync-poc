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
  enum class State { Idle, PulseOn, PulseOff };

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
  unsigned long phaseStartedAtMs_ = 0;
  int pulseOnMs_ = 0;
  int pulseOffMs_ = 0;
  int targetPulses_ = 0;
  int completedPulses_ = 0;
  int accumulatedOnMs_ = 0;
};
