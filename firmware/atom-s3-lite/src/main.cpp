#include <Arduino.h>
#include <M5Unified.h>

#include "Config.h"
#include "CalibrationService.h"
#include "MoistureSensor.h"
#include "PumpController.h"
#include "WateringSettings.h"
#include "WiFiService.h"
#include "MQTTService.h"
#include "OtaService.h"

MoistureSensor moistureSensor;
PumpController pump;
WateringSettings settings;
WiFiService wifi;
MQTTService mqtt;
OtaService ota;
CalibrationService calibration;

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
bool lastSampleBelowCalibrationRange = false;
bool lastSampleAboveCalibrationRange = false;

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
  const MoistureReading reading = moistureSensor.read();
  lastRaw = reading.raw;
  lastPercent = reading.percent;
  lastSampleBelowCalibrationRange = reading.belowCalibrationRange;
  lastSampleAboveCalibrationRange = reading.aboveCalibrationRange;
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
  settings.begin();
  moistureSensor.begin(&settings);

  Serial.println();
  Serial.print("===== GreenSync Firmware v");
  Serial.print(Config::FirmwareVersion);
  Serial.println(" MQTT + OTA =====");

  wifi.begin();
  calibration.begin(&moistureSensor, &settings, &mqtt);
  mqtt.begin(&settings, &ota, &calibration);
  ota.begin(&mqtt);
}

void loop() {
  M5.update();
  wifi.loop();
  mqtt.loop();
  const unsigned long nowMs = millis();
  bool shouldPublish = false;

  if (M5.BtnA.pressedFor(Config::EmergencyStopHoldMs) &&
      controllerState != ControllerState::EmergencyStop) {
    enterEmergencyStop();
    shouldPublish = true;
  }

  const bool calibrationButtonClicked =
      calibration.isActive() && M5.BtnA.wasClicked();

  if (controllerState == ControllerState::Watering &&
      nowMs - wateringStartedAtMs >= static_cast<unsigned long>(Config::WateringDurationMs)) {
    stopWatering();
    shouldPublish = true;
  }

  if (ota.hasPendingCommand() && controllerState == ControllerState::Watering) {
    stopWatering();
    shouldPublish = true;
  }
  if (calibration.hasPendingCommand() &&
      controllerState == ControllerState::Watering) {
    stopWatering();
    shouldPublish = true;
  }
  calibration.loop(
      controllerState == ControllerState::Idle,
      controllerState == ControllerState::EmergencyStop,
      ota.isBusy() || ota.isPendingVerification() || ota.hasPendingCommand(),
      calibrationButtonClicked);
  ota.loop(controllerState == ControllerState::Idle,
           controllerState == ControllerState::EmergencyStop);
  const bool wateringInhibited =
      ota.isBusy() || ota.isPendingVerification() || ota.hasPendingCommand() ||
      calibration.isActive() || calibration.hasPendingCommand();

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
    Serial.print(stateName(controllerState));
    if (lastSampleBelowCalibrationRange) {
      Serial.print(", calibration=BELOW_WET_RAW");
    } else if (lastSampleAboveCalibrationRange) {
      Serial.print(", calibration=ABOVE_DRY_RAW");
    } else {
      Serial.print(", calibration=IN_RANGE");
    }
    Serial.println();

    if (!wateringInhibited && controllerState == ControllerState::Idle &&
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
