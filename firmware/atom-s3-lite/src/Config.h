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
}
