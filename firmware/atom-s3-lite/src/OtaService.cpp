#include "OtaService.h"

#include "Config.h"
#include "MQTTService.h"
#include "Secrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <time.h>

#ifndef OTA_BASE_URL
#define OTA_BASE_URL ""
#endif

#ifndef OTA_MANIFEST_URL
#define OTA_MANIFEST_URL ""
#endif

#ifndef OTA_CA_CERT
#define OTA_CA_CERT ""
#endif

namespace {
constexpr size_t MaxCommandBytes = 768;
constexpr size_t MaxManifestBytes = 4096;
constexpr size_t DownloadBufferBytes = 4096;
constexpr time_t MinimumValidEpoch = 1704067200;  // 2024-01-01T00:00:00Z
constexpr unsigned long TimeSyncTimeoutMs = 15000;

bool ensureSystemTime() {
  if (time(nullptr) >= MinimumValidEpoch) return true;

  Serial.println("Synchronizing clock for TLS certificate validation...");
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
  const unsigned long startedAtMs = millis();
  while (millis() - startedAtMs < TimeSyncTimeoutMs) {
    if (time(nullptr) >= MinimumValidEpoch) {
      Serial.println("Clock synchronized");
      return true;
    }
    delay(100);
  }
  Serial.println("Clock synchronization timed out");
  return false;
}

bool startsWithAllowedBase(const char* url) {
  const size_t baseLength = strlen(OTA_BASE_URL);
  if (baseLength == 0 || strncmp(url, OTA_BASE_URL, baseLength) != 0) {
    return false;
  }
  const char boundary = url[baseLength];
  return boundary == '\0' || boundary == '/';
}

bool parseVersion(const char* version, int result[3]) {
  return version != nullptr &&
         sscanf(version, "%d.%d.%d", &result[0], &result[1], &result[2]) == 3;
}

bool isNewerVersion(const char* candidate) {
  int current[3];
  int next[3];
  if (!parseVersion(Config::FirmwareVersion, current) ||
      !parseVersion(candidate, next)) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    if (next[i] != current[i]) {
      return next[i] > current[i];
    }
  }
  return false;
}

void sha256ToHex(const unsigned char hash[32], char output[65]) {
  for (size_t i = 0; i < 32; ++i) {
    snprintf(output + i * 2, 3, "%02x", hash[i]);
  }
  output[64] = '\0';
}
}

void OtaService::begin(MQTTService* mqttService) {
  mqtt_ = mqttService;
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (running != nullptr &&
      esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    pendingVerification_ = true;
    verificationStartedAtMs_ = millis();
    Serial.println("OTA pending verification; watering remains disabled");
  }
}

bool OtaService::queueCommand(const byte* payload, unsigned int length) {
  if (length == 0 || length > MaxCommandBytes || busy_ || pendingVerification_ ||
      !queuedCommand_.isEmpty()) {
    return false;
  }
  queuedCommand_.reserve(length + 1);
  queuedCommand_ = "";
  for (unsigned int i = 0; i < length; ++i) {
    queuedCommand_ += static_cast<char>(payload[i]);
  }
  return true;
}

void OtaService::loop(bool controllerIdle, bool emergencyStopActive) {
  checkPendingVerification();
  if (pendingVerification_ || busy_ || queuedCommand_.isEmpty()) {
    return;
  }
  processCommand(controllerIdle, emergencyStopActive);
  queuedCommand_ = "";
}

bool OtaService::processCommand(bool controllerIdle, bool emergencyStopActive) {
  JsonDocument command;
  if (deserializeJson(command, queuedCommand_) != DeserializationError::Ok) {
    fail("", "", "INVALID_COMMAND", "Command is not valid JSON");
    return false;
  }

  const char* action = command["action"] | "";
  const char* requestId = command["requestId"] | "";
  const char* targetVersion = command["targetVersion"] | "";
  const char* requestedUrl = command["manifestUrl"] | "";
  const char* manifestUrl = strlen(requestedUrl) > 0 ? requestedUrl : OTA_MANIFEST_URL;
  if ((strcmp(action, "install") != 0 && strcmp(action, "check") != 0) ||
      strlen(requestId) == 0 || strlen(targetVersion) == 0) {
    fail(requestId, targetVersion, "INVALID_COMMAND", "Required fields are missing");
    return false;
  }
  if (lastRequestId_ == requestId) {
    fail(requestId, targetVersion, "DUPLICATE_REQUEST", "Request was already processed");
    return false;
  }
  lastRequestId_ = requestId;
  if (emergencyStopActive) {
    fail(requestId, targetVersion, "EMERGENCY_STOP_ACTIVE", "Emergency stop is active");
    return false;
  }
  if (!controllerIdle) {
    fail(requestId, targetVersion, "BUSY", "Controller is not idle");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED || mqtt_ == nullptr || !mqtt_->isConnected()) {
    fail(requestId, targetVersion, "NETWORK_UNAVAILABLE", "WiFi or MQTT is unavailable");
    return false;
  }
  if (!startsWithAllowedBase(manifestUrl)) {
    fail(requestId, targetVersion, "URL_NOT_ALLOWED", "Manifest URL is not allowed");
    return false;
  }

  busy_ = true;
  const bool result = fetchAndInstall(manifestUrl, requestId, targetVersion,
                                      strcmp(action, "install") == 0);
  busy_ = false;
  return result;
}

bool OtaService::fetchAndInstall(const char* manifestUrl, const char* requestId,
                                 const char* targetVersion, bool install) {
  if (strlen(OTA_CA_CERT) == 0) {
    fail(requestId, targetVersion, "TLS_ERROR", "OTA CA certificate is not configured");
    return false;
  }
  if (!ensureSystemTime()) {
    fail(requestId, targetVersion, "TIME_SYNC_FAILED", "Could not synchronize clock for TLS");
    return false;
  }

  publish("CHECKING", requestId, targetVersion, 0, nullptr, "Fetching manifest");
  WiFiClientSecure tlsClient;
  tlsClient.setCACert(OTA_CA_CERT);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(Config::OtaHttpTimeoutMs);
  if (!http.begin(tlsClient, manifestUrl)) {
    fail(requestId, targetVersion, "TLS_ERROR", "Could not initialize HTTPS client");
    return false;
  }
  const int status = http.GET();
  Serial.print("OTA manifest HTTP status: ");
  Serial.print(status);
  if (status < 0) {
    Serial.print(" (");
    Serial.print(HTTPClient::errorToString(status));
    Serial.print(")");
  }
  Serial.println();
  if (status != HTTP_CODE_OK) {
    http.end();
    fail(requestId, targetVersion, "DOWNLOAD_FAILED", "Manifest download failed");
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength <= 0 || contentLength > static_cast<int>(MaxManifestBytes)) {
    http.end();
    fail(requestId, targetVersion, "MANIFEST_INVALID", "Manifest size is invalid");
    return false;
  }
  const String body = http.getString();
  http.end();

  JsonDocument manifest;
  if (deserializeJson(manifest, body) != DeserializationError::Ok) {
    fail(requestId, targetVersion, "MANIFEST_INVALID", "Manifest JSON is invalid");
    return false;
  }
  const char* hardware = manifest["hardware"] | "";
  const char* version = manifest["version"] | "";
  const char* firmwareUrl = manifest["url"] | "";
  const char* sha256 = manifest["sha256"] | "";
  const size_t size = manifest["size"] | 0;
  const int schemaVersion = manifest["schemaVersion"] | 0;
  if (schemaVersion != 1) {
    fail(requestId, targetVersion, "MANIFEST_INVALID", "Manifest schema is not supported");
    return false;
  }
  if (strcmp(hardware, Config::HardwareId) != 0) {
    fail(requestId, targetVersion, "HARDWARE_MISMATCH", "Manifest hardware does not match");
    return false;
  }
  if (strcmp(version, targetVersion) != 0 || !isNewerVersion(version)) {
    fail(requestId, targetVersion, "VERSION_NOT_ALLOWED", "Version is not newer or does not match");
    return false;
  }
  if (size == 0 || size > static_cast<size_t>(Config::OtaMaxImageBytes)) {
    fail(requestId, targetVersion, "IMAGE_TOO_LARGE", "Firmware size is invalid");
    return false;
  }
  if (strlen(sha256) != 64 || !startsWithAllowedBase(firmwareUrl)) {
    fail(requestId, targetVersion, "MANIFEST_INVALID", "Manifest hash or URL is invalid");
    return false;
  }
  if (!install) {
    publish("AVAILABLE", requestId, targetVersion, 0, nullptr, "Update is available");
    return true;
  }
  return downloadFirmware(firmwareUrl, size, sha256, requestId, targetVersion);
}

bool OtaService::downloadFirmware(const char* url, size_t expectedSize,
                                  const char* expectedSha256,
                                  const char* requestId,
                                  const char* targetVersion) {
  WiFiClientSecure tlsClient;
  tlsClient.setCACert(OTA_CA_CERT);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setTimeout(Config::OtaHttpTimeoutMs);
  if (!http.begin(tlsClient, url)) {
    fail(requestId, targetVersion, "TLS_ERROR", "Could not initialize firmware HTTPS client");
    return false;
  }
  const int firmwareStatus = http.GET();
  Serial.print("OTA firmware HTTP status: ");
  Serial.print(firmwareStatus);
  if (firmwareStatus < 0) {
    Serial.print(" (");
    Serial.print(HTTPClient::errorToString(firmwareStatus));
    Serial.print(")");
  }
  Serial.println();
  if (firmwareStatus != HTTP_CODE_OK ||
      http.getSize() != static_cast<int>(expectedSize)) {
    http.end();
    fail(requestId, targetVersion, "DOWNLOAD_FAILED", "Firmware download failed");
    return false;
  }
  if (!Update.begin(expectedSize, U_FLASH)) {
    http.end();
    fail(requestId, targetVersion, "IMAGE_TOO_LARGE", Update.errorString());
    return false;
  }

  publish("DOWNLOADING", requestId, targetVersion, 0, nullptr, "Downloading firmware");
  mbedtls_sha256_context shaContext;
  mbedtls_sha256_init(&shaContext);
  mbedtls_sha256_starts_ret(&shaContext, 0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[DownloadBufferBytes];
  size_t written = 0;
  int lastProgress = -1;
  while (written < expectedSize) {
    const size_t available = stream->available();
    if (available == 0) {
      if (!http.connected()) break;
      delay(1);
      continue;
    }
    const size_t requested = min(available, min(sizeof(buffer), expectedSize - written));
    const int read = stream->readBytes(buffer, requested);
    if (read <= 0 || Update.write(buffer, read) != static_cast<size_t>(read)) break;
    mbedtls_sha256_update_ret(&shaContext, buffer, read);
    written += read;
    const int progress = static_cast<int>((written * 100) / expectedSize);
    if (progress >= lastProgress + 10) {
      lastProgress = progress;
      publish("DOWNLOADING", requestId, targetVersion, progress, nullptr, "Downloading firmware");
    }
  }
  http.end();
  unsigned char hash[32];
  mbedtls_sha256_finish_ret(&shaContext, hash);
  mbedtls_sha256_free(&shaContext);
  char actualSha256[65];
  sha256ToHex(hash, actualSha256);

  if (written != expectedSize) {
    Update.abort();
    fail(requestId, targetVersion, "DOWNLOAD_FAILED", "Firmware download was incomplete");
    return false;
  }
  publish("VERIFYING", requestId, targetVersion, 100, nullptr, "Verifying firmware");
  if (strcasecmp(actualSha256, expectedSha256) != 0) {
    Update.abort();
    fail(requestId, targetVersion, "HASH_MISMATCH", "Firmware SHA-256 does not match");
    return false;
  }
  if (!Update.end(true)) {
    fail(requestId, targetVersion, "FLASH_WRITE_FAILED", Update.errorString());
    return false;
  }
  publish("REBOOTING", requestId, targetVersion, 100, nullptr, "Rebooting into new firmware");
  delay(500);
  ESP.restart();
  return true;
}

void OtaService::checkPendingVerification() {
  if (!pendingVerification_) return;
  if (WiFi.status() == WL_CONNECTED && mqtt_ != nullptr && mqtt_->isConnected()) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      pendingVerification_ = false;
      publish("SUCCEEDED", "", Config::FirmwareVersion, 100, nullptr, "New firmware verified");
    }
    return;
  }
  if (millis() - verificationStartedAtMs_ >=
      static_cast<unsigned long>(Config::OtaVerificationTimeoutMs)) {
    publish("ROLLED_BACK", "", Config::FirmwareVersion, 0,
            "BOOT_VALIDATION_FAILED", "Connectivity health check failed");
    delay(200);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

void OtaService::publish(const char* state, const char* requestId,
                         const char* targetVersion, int progress,
                         const char* errorCode, const char* message) {
  if (mqtt_ != nullptr) {
    mqtt_->publishOtaState(state, requestId, targetVersion, progress,
                           errorCode, message);
  }
}

void OtaService::fail(const char* requestId, const char* targetVersion,
                      const char* errorCode, const char* message) {
  publish("FAILED", requestId, targetVersion, 0, errorCode, message);
}

bool OtaService::isBusy() const { return busy_; }
bool OtaService::isPendingVerification() const { return pendingVerification_; }
bool OtaService::hasPendingCommand() const { return !queuedCommand_.isEmpty(); }
