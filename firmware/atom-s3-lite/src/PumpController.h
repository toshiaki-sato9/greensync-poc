#pragma once

class PumpController {
public:
  void begin();
  void off();
  void on();
  void setDutyPercent(int dutyPercent);
  int dutyPercent() const;
  void waterForMs(int durationMs);

private:
  int dutyPercent_ = 0;
};
