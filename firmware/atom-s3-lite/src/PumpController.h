#pragma once

class PumpController {
public:
  void begin();
  void off();
  void on();
  void waterForMs(int durationMs);
};
