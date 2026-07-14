#pragma once

class WateringSettings;

class MQTTService {
public:
  void begin(WateringSettings* settings);
  void loop();
  void publishDiscovery();
  bool publishState(int raw, int moisturePercent, int rssi, bool watered);
  bool publishSettings();

private:
  void connect();
};
