#include "WateringSettings.h"

#include "Config.h"

#include <Preferences.h>

namespace {
constexpr char PreferencesNamespace[] = "greensync";
constexpr char ThresholdKey[] = "threshold";
constexpr char CalibrationKey[] = "moistureCal";
constexpr char WateringLockoutKey[] = "waterLock";
constexpr int DefaultThresholdPercent = 30;
constexpr uint32_t CalibrationMagic = 0x47534331;

struct CalibrationData {
  uint32_t magic;
  int32_t dryRaw;
  int32_t wetRaw;
};
}

void WateringSettings::begin() {
  Preferences preferences;
  preferences.begin(PreferencesNamespace, true);
  wateringThresholdPercent_ =
      clampThresholdPercent(preferences.getInt(ThresholdKey, DefaultThresholdPercent));
  wateringLockout_ = preferences.getBool(WateringLockoutKey, false);
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

bool WateringSettings::wateringLockout() const { return wateringLockout_; }

bool WateringSettings::setWateringLockout(bool active) {
  Preferences preferences;
  if (!preferences.begin(PreferencesNamespace, false)) return false;
  const bool saved =
      preferences.putBool(WateringLockoutKey, active) == sizeof(bool);
  preferences.end();
  if (!saved) return false;
  wateringLockout_ = active;
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
