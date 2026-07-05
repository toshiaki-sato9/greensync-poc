#pragma once

class MQTTService {
public:
  void begin();
  void loop();
  void publishDiscovery();
  bool publishState(int raw, int moisturePercent, int rssi, bool watered);

private:
  void connect();
};