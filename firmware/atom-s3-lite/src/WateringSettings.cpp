#include "WateringSettings.h"

#include "Config.h"

#include <Preferences.h>

namespace {
constexpr char PreferencesNamespace[] = "greensync";
constexpr char ThresholdKey[] = "threshold";
constexpr char CalibrationKey[] = "moistureCal";
constexpr char PumpCalibrationKey[] = "pumpCal";
constexpr int DefaultThresholdPercent = 30;
constexpr uint32_t CalibrationMagic = 0x47534331;
constexpr uint32_t PumpCalibrationMagic = 0x47535031;

struct CalibrationData {
  uint32_t magic;
  int32_t dryRaw;
  int32_t wetRaw;
};

struct PumpCalibrationData {
  uint32_t magic;
  int32_t dutyPercent;
  int32_t flowMilliMlPerSecond;
  int32_t referenceDurationMs;
  int32_t referenceVolumeMl;
};
}

void WateringSettings::begin() {
  Preferences preferences;
  preferences.begin(PreferencesNamespace, true);
  wateringThresholdPercent_ =
      clampThresholdPercent(preferences.getInt(ThresholdKey, DefaultThresholdPercent));
  CalibrationData calibration{};
  const bool calibrationRead =
      preferences.getBytesLength(CalibrationKey) == sizeof(calibration) &&
      preferences.getBytes(CalibrationKey, &calibration,
                           sizeof(calibration)) == sizeof(calibration);
  if (calibrationRead && calibration.magic == CalibrationMagic &&
      calibration.wetRaw >= 0 && calibration.dryRaw <= 4095 &&
      calibration.dryRaw > calibration.wetRaw &&
      calibration.dryRaw - calibration.wetRaw >=
          Config::CalibrationMinimumSpanRaw) {
    moistureDryRaw_ = calibration.dryRaw;
    moistureWetRaw_ = calibration.wetRaw;
  } else {
    moistureDryRaw_ = Config::DryRaw;
    moistureWetRaw_ = Config::WetRaw;
  }
  PumpCalibrationData pumpCalibration{};
  const bool pumpCalibrationRead =
      preferences.getBytesLength(PumpCalibrationKey) ==
          sizeof(pumpCalibration) &&
      preferences.getBytes(PumpCalibrationKey, &pumpCalibration,
                           sizeof(pumpCalibration)) == sizeof(pumpCalibration);
  if (pumpCalibrationRead &&
      pumpCalibration.magic == PumpCalibrationMagic &&
      pumpCalibration.dutyPercent >= 1 &&
      pumpCalibration.dutyPercent <= 100 &&
      pumpCalibration.flowMilliMlPerSecond >= 10 &&
      pumpCalibration.flowMilliMlPerSecond <= 50000 &&
      pumpCalibration.referenceDurationMs >= 1000 &&
      pumpCalibration.referenceDurationMs <= 60000 &&
      pumpCalibration.referenceVolumeMl >= 1 &&
      pumpCalibration.referenceVolumeMl <= 1000) {
    hasPumpCalibration_ = true;
    pumpDutyPercent_ = pumpCalibration.dutyPercent;
    pumpFlowMilliMlPerSecond_ = pumpCalibration.flowMilliMlPerSecond;
    pumpReferenceDurationMs_ = pumpCalibration.referenceDurationMs;
    pumpReferenceVolumeMl_ = pumpCalibration.referenceVolumeMl;
  }
  preferences.end();
}

int WateringSettings::moistureDryRaw() const { return moistureDryRaw_; }

int WateringSettings::moistureWetRaw() const { return moistureWetRaw_; }

bool WateringSettings::setMoistureCalibration(int dryRaw, int wetRaw) {
  if (wetRaw < 0 || dryRaw > 4095 || dryRaw <= wetRaw ||
      dryRaw - wetRaw < Config::CalibrationMinimumSpanRaw) {
    return false;
  }
  const CalibrationData calibration = {CalibrationMagic, dryRaw, wetRaw};
  Preferences preferences;
  if (!preferences.begin(PreferencesNamespace, false)) return false;
  const bool saved =
      preferences.putBytes(CalibrationKey, &calibration,
                           sizeof(calibration)) == sizeof(calibration);
  preferences.end();
  if (!saved) return false;
  moistureDryRaw_ = dryRaw;
  moistureWetRaw_ = wetRaw;
  return true;
}

bool WateringSettings::hasPumpCalibration() const {
  return hasPumpCalibration_;
}

int WateringSettings::pumpDutyPercent() const { return pumpDutyPercent_; }

int WateringSettings::pumpFlowMilliMlPerSecond() const {
  return pumpFlowMilliMlPerSecond_;
}

int WateringSettings::pumpReferenceDurationMs() const {
  return pumpReferenceDurationMs_;
}

int WateringSettings::pumpReferenceVolumeMl() const {
  return pumpReferenceVolumeMl_;
}

bool WateringSettings::setPumpCalibration(int dutyPercent,
                                          int flowMilliMlPerSecond,
                                          int referenceDurationMs,
                                          int referenceVolumeMl) {
  if (dutyPercent < 1 || dutyPercent > 100 ||
      flowMilliMlPerSecond < 10 || flowMilliMlPerSecond > 50000 ||
      referenceDurationMs < 1000 || referenceDurationMs > 60000 ||
      referenceVolumeMl < 1 || referenceVolumeMl > 1000) {
    return false;
  }
  const PumpCalibrationData calibration = {
      PumpCalibrationMagic, dutyPercent, flowMilliMlPerSecond,
      referenceDurationMs, referenceVolumeMl};
  Preferences preferences;
  if (!preferences.begin(PreferencesNamespace, false)) return false;
  const bool saved =
      preferences.putBytes(PumpCalibrationKey, &calibration,
                           sizeof(calibration)) == sizeof(calibration);
  preferences.end();
  if (!saved) return false;
  hasPumpCalibration_ = true;
  pumpDutyPercent_ = dutyPercent;
  pumpFlowMilliMlPerSecond_ = flowMilliMlPerSecond;
  pumpReferenceDurationMs_ = referenceDurationMs;
  pumpReferenceVolumeMl_ = referenceVolumeMl;
  return true;
}

int WateringSettings::wateringThresholdPercent() const {
  return wateringThresholdPercent_;
}

bool WateringSettings::setWateringThresholdPercent(int value) {
  const int normalized = clampThresholdPercent(value);
  if (normalized == wateringThresholdPercent_) {
    return false;
  }

  wateringThresholdPercent_ = normalized;

  Preferences preferences;
  preferences.begin(PreferencesNamespace, false);
  preferences.putInt(ThresholdKey, wateringThresholdPercent_);
  preferences.end();

  return true;
}

int WateringSettings::clampThresholdPercent(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}
