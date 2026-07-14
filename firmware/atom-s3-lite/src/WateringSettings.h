#pragma once

class WateringSettings {
public:
  void begin();
  int wateringThresholdPercent() const;
  bool setWateringThresholdPercent(int value);
  static int clampThresholdPercent(int value);

private:
  int wateringThresholdPercent_ = 30;
};
