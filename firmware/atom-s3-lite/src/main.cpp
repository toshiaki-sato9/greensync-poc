#include <Arduino.h>
#include <M5Unified.h>

#include "Config.h"
#include "FirmwareInfo.h"
#include "MoistureSensor.h"
#include "PumpController.h"
#include "WateringSettings.h"
#include "WiFiService.h"
#include "MQTTService.h"

MoistureSensor moistureSensor;
PumpController pump;
WateringSettings settings;
WiFiService wifi;
MQTTService mqtt;

namespace {
enum class ControllerState {
  Idle,
  Watering,
  EmergencyStop,
};

ControllerState controllerState = ControllerState::Idle;
unsigned long lastTelemetryAtMs = 0;
unsigned long wateringStartedAtMs = 0;
int lastRaw = 0;
int lastPercent = 0;
bool hasSensorSample = false;

const char* stateName(ControllerState state) {
  switch (state) {
    case ControllerState::Idle:
      return "IDLE";
    case ControllerState::Watering:
      return "WATERING";
    case ControllerState::EmergencyStop:
      return "EMERGENCY_STOP";
  }
  return "UNKNOWN";
}

void readMoistureSample() {
  lastRaw = moistureSensor.readRaw();
  lastPercent = moistureSensor.readPercent();
  hasSensorSample = true;
}

void publishCurrentState() {
  if (!hasSensorSample) {
    readMoistureSample();
  }

  mqtt.publishState(
      lastRaw, lastPercent, wifi.rssi(), controllerState == ControllerState::Watering);
  mqtt.publishSettings();
}

void startWatering(unsigned long nowMs) {
  controllerState = ControllerState::Watering;
  wateringStartedAtMs = nowMs;
  pump.on();
  Serial.println("Soil is dry. Watering...");
}

void stopWatering() {
  pump.off();
  Serial.println("Watering done.");
  controllerState = ControllerState::Idle;
}

void enterEmergencyStop() {
  pump.off();
  controllerState = ControllerState::EmergencyStop;
  Serial.println("EMERGENCY STOP: Pump forced OFF.");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(2000);

  auto cfg = M5.config();
  M5.begin(cfg);

  pump.begin();
  pump.off();

  Serial.println();
  Serial.print("===== GreenSync Firmware v");
  Serial.print(FirmwareInfo::Version);
  Serial.println(" MQTT =====");

  settings.begin();
  wifi.begin();
  mqtt.begin(&settings);
}

void loop() {
  M5.update();
  mqtt.loop();
  const unsigned long nowMs = millis();
  bool shouldPublish = false;

  if (M5.BtnA.pressedFor(Config::EmergencyStopHoldMs) &&
      controllerState != ControllerState::EmergencyStop) {
    enterEmergencyStop();
    shouldPublish = true;
  }

  if (controllerState == ControllerState::Watering &&
      nowMs - wateringStartedAtMs >= static_cast<unsigned long>(Config::WateringDurationMs)) {
    stopWatering();
    shouldPublish = true;
  }

  if (!hasSensorSample ||
      nowMs - lastTelemetryAtMs >= static_cast<unsigned long>(Config::TelemetryIntervalMs)) {
    lastTelemetryAtMs = nowMs;
    readMoistureSample();

    Serial.print("raw=");
    Serial.print(lastRaw);
    Serial.print(", moisture=");
    Serial.print(lastPercent);
    Serial.print("%, threshold=");
    Serial.print(settings.wateringThresholdPercent());
    Serial.print("%, state=");
    Serial.println(stateName(controllerState));

    if (controllerState == ControllerState::Idle &&
        lastPercent < settings.wateringThresholdPercent()) {
      startWatering(nowMs);
    }

    shouldPublish = true;
  }

  if (shouldPublish) {
    publishCurrentState();
    Serial.println("--------------------");
  }
}
