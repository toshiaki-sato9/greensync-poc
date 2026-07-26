#pragma once

#include <Arduino.h>

class MQTTService;

class OtaService {
public:
  void begin(MQTTService* mqttService);
  void loop(bool controllerIdle, bool emergencyStopActive);
  bool queueCommand(const byte* payload, unsigned int length);
  bool isBusy() const;
  bool isPendingVerification() const;
  bool hasPendingCommand() const;

private:
  bool processCommand(bool controllerIdle, bool emergencyStopActive);
  bool fetchAndInstall(const char* manifestUrl, const char* requestId,
                       const char* targetVersion, bool install);
  bool downloadFirmware(const char* url, size_t expectedSize,
                        const char* expectedSha256, const char* requestId,
                        const char* targetVersion);
  void publish(const char* state, const char* requestId,
               const char* targetVersion, int progress,
               const char* errorCode, const char* message);
  void fail(const char* requestId, const char* targetVersion,
            const char* errorCode, const char* message);
  void checkPendingVerification();
  bool savePendingResult(const char* requestId, const char* targetVersion);
  void loadPendingResult();
  void clearPendingResult();

  MQTTService* mqtt_ = nullptr;
  String queuedCommand_;
  String lastRequestId_;
  bool busy_ = false;
  bool pendingVerification_ = false;
  bool pendingResult_ = false;
  String pendingRequestId_;
  String pendingTargetVersion_;
  unsigned long verificationStartedAtMs_ = 0;
};
