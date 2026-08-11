#include <Arduino.h>
#include <M5Unified.h>

#include "Config.h"
#include "CalibrationService.h"
#include "MoistureSensor.h"
#include "PumpController.h"
#include "PumpEvaluationService.h"
#include "WateringSettings.h"
#include "WiFiService.h"
#include "MQTTService.h"
#include "OtaService.h"

MoistureSensor moistureSensor;
PumpController pump;
PumpEvaluationService pumpEvaluation;
WateringSettings settings;
WiFiService wifi;
MQTTService mqtt;
OtaService ota;
CalibrationService calibration;

namespace {
enum class ControllerState {
  Idle,
  Watering,
  Soaking,
  WateringLockout,
  EmergencyStop,
};

ControllerState controllerState = ControllerState::Idle;
unsigned long lastTelemetryAtMs = 0;
unsigned long wateringStartedAtMs = 0;
unsigned long soakingStartedAtMs = 0;
int wateringPulseCount = 0;
int wateringCycleStartPercent = 0;
const char* wateringFaultCode = "";
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
    case ControllerState::Soaking:
      return "SOAKING";
    case ControllerState::WateringLockout:
      return "WATERING_LOCKOUT";
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
      lastRaw, lastPercent, wifi.rssi(),
      controllerState == ControllerState::Watering || pumpEvaluation.isPumpOn(),
      stateName(controllerState), wateringPulseCount,
      controllerState == ControllerState::WateringLockout,
      wateringFaultCode);
  mqtt.publishSettings();
}

void startWateringPulse(unsigned long nowMs) {
  controllerState = ControllerState::Watering;
  wateringStartedAtMs = nowMs;
  pump.setDutyPercent(Config::PumpWateringDutyPercent);
  Serial.print("Watering pulse started: ");
  Serial.print(wateringPulseCount + 1);
  Serial.print("/");
  Serial.println(Config::WateringMaxPulses);
}

void finishWateringPulse(unsigned long nowMs) {
  pump.off();
  ++wateringPulseCount;
  soakingStartedAtMs = nowMs;
  controllerState = ControllerState::Soaking;
  Serial.println("Watering pulse completed. Soil soaking period started.");
}

void finishWateringCycle(const char* message) {
  pump.off();
  controllerState = ControllerState::Idle;
  wateringPulseCount = 0;
  wateringFaultCode = "";
  Serial.println(message);
}

void lockoutWatering(const char* faultCode, const char* message) {
  pump.off();
  controllerState = ControllerState::WateringLockout;
  wateringFaultCode = faultCode;
  Serial.print("Watering locked out: ");
  Serial.print(faultCode);
  Serial.print(" - ");
  Serial.println(message);
}

void cancelWateringCycle(const char* reason) {
  if (controllerState == ControllerState::Watering ||
      controllerState == ControllerState::Soaking) {
    finishWateringCycle(reason);
  }
}

void startWateringCycle(unsigned long nowMs) {
  wateringCycleStartPercent = lastPercent;
  wateringPulseCount = 0;
  wateringFaultCode = "";
  startWateringPulse(nowMs);
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
  pumpEvaluation.begin(&pump, &mqtt);
  mqtt.begin(&settings, &ota, &calibration, &pumpEvaluation);
  ota.begin(&mqtt);
}

void loop() {
  M5.update();
  const unsigned long nowMs = millis();
  bool shouldPublish = false;

  // Physical emergency stop takes priority over any network operation.
  if (M5.BtnA.pressedFor(Config::EmergencyStopHoldMs) &&
      controllerState != ControllerState::EmergencyStop) {
    enterEmergencyStop();
    shouldPublish = true;
  }

  // Hardware PWM continues independently while MQTT remains responsive to
  // the evaluation cancel command.
  wifi.loop();
  mqtt.loop();

  const bool calibrationButtonClicked =
      calibration.isActive() && M5.BtnA.wasClicked();

  if (controllerState == ControllerState::Watering &&
      nowMs - wateringStartedAtMs >= static_cast<unsigned long>(Config::WateringDurationMs)) {
    finishWateringPulse(nowMs);
    shouldPublish = true;
  }

  if (ota.hasPendingCommand() &&
      (controllerState == ControllerState::Watering ||
       controllerState == ControllerState::Soaking)) {
    cancelWateringCycle("Watering cycle cancelled for OTA");
    shouldPublish = true;
  }
  if (calibration.hasPendingCommand() &&
      (controllerState == ControllerState::Watering ||
       controllerState == ControllerState::Soaking)) {
    cancelWateringCycle("Watering cycle cancelled for moisture calibration");
    shouldPublish = true;
  }
  if (pumpEvaluation.hasPendingCommand() &&
      (controllerState == ControllerState::Watering ||
       controllerState == ControllerState::Soaking)) {
    cancelWateringCycle("Watering cycle cancelled for pump evaluation");
    shouldPublish = true;
  }
  const bool controllerAvailable =
      controllerState == ControllerState::Idle ||
      controllerState == ControllerState::WateringLockout;
  calibration.loop(
      controllerAvailable,
      controllerState == ControllerState::EmergencyStop,
      ota.isBusy() || ota.isPendingVerification() || ota.hasPendingCommand(),
      calibrationButtonClicked);
  pumpEvaluation.loop(
      controllerState == ControllerState::Idle,
      controllerState == ControllerState::EmergencyStop,
      ota.isBusy() || ota.isPendingVerification() || ota.hasPendingCommand(),
      calibration.isActive() || calibration.hasPendingCommand());
  ota.loop(controllerAvailable,
           controllerState == ControllerState::EmergencyStop);
  const bool wateringInhibited =
      ota.isBusy() || ota.isPendingVerification() || ota.hasPendingCommand() ||
      calibration.isActive() || calibration.hasPendingCommand();
  const bool allWateringInhibited =
      wateringInhibited || pumpEvaluation.isActive() ||
      pumpEvaluation.hasPendingCommand();

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

    const int stopThreshold = min(
        100, settings.wateringThresholdPercent() +
                 Config::WateringStopHysteresisPercent);

    if (controllerState == ControllerState::Soaking &&
        nowMs - soakingStartedAtMs >=
            static_cast<unsigned long>(Config::WateringSoakDurationMs)) {
      if (lastPercent >= stopThreshold) {
        finishWateringCycle("Target moisture reached. Watering cycle completed.");
      } else if (wateringPulseCount >= Config::WateringMaxPulses) {
        lockoutWatering("MAX_WATERING_PULSES",
                        "Target moisture was not reached within the safety limit");
      } else if (wateringPulseCount >= 2 &&
                 lastPercent < wateringCycleStartPercent +
                                   Config::WateringMinimumResponsePercent) {
        lockoutWatering("NO_MOISTURE_RESPONSE",
                        "Moisture did not increase after watering");
      } else {
        startWateringPulse(nowMs);
      }
    } else if (controllerState == ControllerState::WateringLockout &&
               lastPercent >= stopThreshold) {
      finishWateringCycle("Watering lockout cleared after moisture recovery.");
    }

    if (Config::AutomaticWateringEnabled && !allWateringInhibited &&
        controllerState == ControllerState::Idle &&
        lastPercent < settings.wateringThresholdPercent()) {
      startWateringCycle(nowMs);
    }

    shouldPublish = true;
  }

  if (shouldPublish) {
    publishCurrentState();
    Serial.println("--------------------");
  }
}
