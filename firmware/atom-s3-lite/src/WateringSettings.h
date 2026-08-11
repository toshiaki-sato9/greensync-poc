#pragma once

class WateringSettings {
public:
  void begin();
  int wateringThresholdPercent() const;
  bool setWateringThresholdPercent(int value);
  int moistureDryRaw() const;
  int moistureWetRaw() const;
  bool setMoistureCalibration(int dryRaw, int wetRaw);
  bool hasPumpCalibration() const;
  int pumpDutyPercent() const;
  int pumpFlowMilliMlPerSecond() const;
  int pumpReferenceDurationMs() const;
  int pumpReferenceVolumeMl() const;
  bool setPumpCalibration(int dutyPercent, int flowMilliMlPerSecond,
                          int referenceDurationMs, int referenceVolumeMl);
  static int clampThresholdPercent(int value);

private:
  int wateringThresholdPercent_ = 30;
  int moistureDryRaw_ = 0;
  int moistureWetRaw_ = 0;
  bool hasPumpCalibration_ = false;
  int pumpDutyPercent_ = 0;
  int pumpFlowMilliMlPerSecond_ = 0;
  int pumpReferenceDurationMs_ = 0;
  int pumpReferenceVolumeMl_ = 0;
};
