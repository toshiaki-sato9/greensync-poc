#pragma once

namespace Config {
constexpr int MoisturePin = 1;
constexpr int PumpPin = 2;

constexpr int DryRaw = 2150;
constexpr int WetRaw = 1770;
constexpr int MoistureSampleCount = 9;
constexpr int MoistureSampleDelayMs = 2;
static_assert(MoistureSampleCount > 0 && MoistureSampleCount % 2 == 1,
              "MoistureSampleCount must be a positive odd number");
static_assert(MoistureSampleDelayMs >= 0,
              "MoistureSampleDelayMs must not be negative");
constexpr int CalibrationMinimumSpanRaw = 100;
constexpr int CalibrationTimeoutMs = 5 * 60 * 1000;

constexpr int WateringThresholdPercent = 30;
constexpr int WateringDurationMs = 10000;
constexpr int WateringActiveMonitorIntervalMs = 1000;
constexpr int WateringSoakDurationMs = 30 * 60 * 1000;
constexpr int WateringMaxPulses = 1;
static_assert(WateringDurationMs <= 10000,
              "Automatic watering pulse must not exceed 10 seconds");
static_assert(WateringActiveMonitorIntervalMs > 0 &&
                  WateringActiveMonitorIntervalMs <= WateringDurationMs,
              "Watering monitor interval must fit within a pulse");
constexpr bool AutomaticWateringEnabled = true;
constexpr int PumpWateringDutyPercent = 50;
constexpr int PumpPwmFrequencyHz = 20000;
constexpr int PumpPwmResolutionBits = 10;
constexpr int PumpPwmChannel = 7;
constexpr int PumpEvaluationMaxDurationMs = 10000;
constexpr int TelemetryIntervalMs = 10000;
constexpr int EmergencyStopHoldMs = 1500;
constexpr int WiFiReconnectIntervalMs = 10000;
constexpr int MqttReconnectIntervalMs = 5000;
constexpr char FirmwareVersion[] = "0.3.26";
constexpr char HardwareId[] = "m5stack-atoms3-lite";
constexpr int OtaHttpTimeoutMs = 15000;
constexpr int OtaVerificationTimeoutMs = 120000;
constexpr int OtaMaxImageBytes = 3 * 1024 * 1024;
}
