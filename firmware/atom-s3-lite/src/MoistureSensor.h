#pragma once

class WateringSettings;

struct MoistureReading {
  int raw;
  int percent;
  bool belowCalibrationRange;
  bool aboveCalibrationRange;
};

class MoistureSensor {
public:
  void begin(WateringSettings* settings);
  MoistureReading read() const;
  int readRaw() const;
  int percentFromRaw(int raw) const;
  int readPercent() const;

private:
  WateringSettings* settings_ = nullptr;
};
