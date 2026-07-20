#pragma once

class WateringSettings;

class MQTTService {
public:
  void begin(WateringSettings* settings);
  void loop();
  bool publishDiscovery();
  bool publishState(int raw, int moisturePercent, int rssi, bool watered);
  bool publishSettings();

private:
  bool connect();
};
