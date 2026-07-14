#include "WateringSettings.h"

#include <Preferences.h>

namespace {
constexpr char PreferencesNamespace[] = "greensync";
constexpr char ThresholdKey[] = "threshold";
constexpr int DefaultThresholdPercent = 30;
}

void WateringSettings::begin() {
  Preferences preferences;
  preferences.begin(PreferencesNamespace, true);
  wateringThresholdPercent_ =
      clampThresholdPercent(preferences.getInt(ThresholdKey, DefaultThresholdPercent));
  preferences.end();
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
