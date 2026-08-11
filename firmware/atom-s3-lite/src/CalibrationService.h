#pragma once

#include <Arduino.h>

class MoistureSensor;
class MQTTService;
class WateringSettings;

class CalibrationService {
public:
  void begin(MoistureSensor* sensor, WateringSettings* settings,
             MQTTService* mqttService);
  bool queueCommand(const byte* payload, unsigned int length);
  void loop(bool controllerIdle, bool emergencyStopActive,
            bool otaUnavailable, bool buttonClicked);
  bool isActive() const;
  bool hasPendingCommand() const;
  bool publishCurrentState();

private:
  enum class State {
    Idle,
    AwaitingDry,
    AwaitingWet,
  };

  void start(bool controllerIdle, bool emergencyStopActive,
             bool otaUnavailable);
  void capturePoint();
  void cancel(const char* message);
  bool publish(const char* state, const char* message, int sampleRaw = -1);

  MoistureSensor* sensor_ = nullptr;
  WateringSettings* settings_ = nullptr;
  MQTTService* mqtt_ = nullptr;
  String queuedCommand_;
  State state_ = State::Idle;
  int pendingDryRaw_ = 0;
  unsigned long startedAtMs_ = 0;
};
