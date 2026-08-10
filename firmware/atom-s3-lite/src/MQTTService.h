#pragma once

class WateringSettings;
class OtaService;
class CalibrationService;
class PumpEvaluationService;

class MQTTService {
public:
  void begin(WateringSettings* settings, OtaService* otaService,
             CalibrationService* calibrationService,
             PumpEvaluationService* pumpEvaluationService);
  void loop();
  bool isConnected() const;
  bool publishDiscovery();
  bool publishState(int raw, int moisturePercent, int rssi, bool watered);
  bool publishSettings();
  bool publishOtaState(const char* state, const char* requestId,
                       const char* targetVersion, int progress,
                       const char* errorCode, const char* message);
  bool publishCalibrationState(const char* state, int dryRaw, int wetRaw,
                               int pendingDryRaw, int sampleRaw,
                               const char* message);
  bool publishPumpEvaluationState(const char* state, const char* profile,
                                  int pulseOnMs, int pulseOffMs,
                                  int targetPulses, int completedPulses,
                                  int accumulatedOnMs, const char* message);

private:
  bool connect();
};
