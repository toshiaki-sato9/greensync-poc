#include "MoistureSensor.h"
#include "Config.h"
#include <Arduino.h>

void MoistureSensor::begin() {
  pinMode(Config::MoisturePin, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::MoisturePin, ADC_11db);

  // Discard the first conversion after ADC configuration.
  analogRead(Config::MoisturePin);
}

int MoistureSensor::readRaw() const {
  int samples[Config::MoistureSampleCount];
  for (int i = 0; i < Config::MoistureSampleCount; ++i) {
    samples[i] = analogRead(Config::MoisturePin);
    delay(Config::MoistureSampleDelayMs);
  }

  // An insertion sort is sufficient for this small fixed-size sample set.
  for (int i = 1; i < Config::MoistureSampleCount; ++i) {
    const int value = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > value) {
      samples[j + 1] = samples[j];
      --j;
    }
    samples[j + 1] = value;
  }

  return samples[Config::MoistureSampleCount / 2];
}

int MoistureSensor::percentFromRaw(int raw) const {
  int percent = map(raw, Config::DryRaw, Config::WetRaw, 0, 100);
  return constrain(percent, 0, 100);
}

MoistureReading MoistureSensor::read() const {
  const int raw = readRaw();
  const int calibrationMin = min(Config::DryRaw, Config::WetRaw);
  const int calibrationMax = max(Config::DryRaw, Config::WetRaw);

  return {
      raw,
      percentFromRaw(raw),
      raw < calibrationMin,
      raw > calibrationMax,
  };
}

int MoistureSensor::readPercent() const {
  return percentFromRaw(readRaw());
}
