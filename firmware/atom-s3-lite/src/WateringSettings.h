#pragma once

class WateringSettings {
public:
  void begin();
  int wateringThresholdPercent() const;
  bool setWateringThresholdPercent(int value);
  int moistureDryRaw() const;
  int moistureWetRaw() const;
  bool setMoistureCalibration(int dryRaw, int wetRaw);
  bool wateringLockout() const;
  bool setWateringLockout(bool active);
  static int clampThresholdPercent(int value);

private:
  int wateringThresholdPercent_ = 30;
  int moistureDryRaw_ = 0;
  int moistureWetRaw_ = 0;
  bool wateringLockout_ = false;
};
