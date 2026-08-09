#pragma once

struct MoistureReading {
  int raw;
  int percent;
  bool belowCalibrationRange;
  bool aboveCalibrationRange;
};

class MoistureSensor {
public:
  void begin();
  MoistureReading read() const;
  int readRaw() const;
  int percentFromRaw(int raw) const;
  int readPercent() const;
};
