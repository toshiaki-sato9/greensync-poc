#pragma once

namespace Config {
constexpr int MoisturePin = 1;
constexpr int PumpPin = 2;

constexpr int DryRaw = 2150;
constexpr int WetRaw = 1770;

constexpr int WateringThresholdPercent = 30;
constexpr int WateringDurationMs = 3000;
constexpr int TelemetryIntervalMs = 10000;
constexpr int EmergencyStopHoldMs = 1500;
constexpr int WiFiReconnectIntervalMs = 10000;
constexpr int MqttReconnectIntervalMs = 5000;
constexpr char FirmwareVersion[] = "0.3.1";
constexpr char HardwareId[] = "m5stack-atoms3-lite";
constexpr int OtaHttpTimeoutMs = 15000;
constexpr int OtaVerificationTimeoutMs = 120000;
constexpr int OtaMaxImageBytes = 3 * 1024 * 1024;
}
