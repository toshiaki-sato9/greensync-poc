#pragma once

class WateringSettings;
class OtaService;

class MQTTService {
public:
  void begin(WateringSettings* settings, OtaService* otaService);
  void loop();
  bool isConnected() const;
  bool publishDiscovery();
  bool publishState(int raw, int moisturePercent, int rssi, bool watered);
  bool publishSettings();
  bool publishOtaState(const char* state, const char* requestId,
                       const char* targetVersion, int progress,
                       const char* errorCode, const char* message);

private:
  bool connect();
};
