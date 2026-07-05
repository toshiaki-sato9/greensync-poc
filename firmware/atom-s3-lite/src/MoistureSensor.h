#pragma once

class MoistureSensor {
public:
  int readRaw() const;
  int readPercent() const;
};
