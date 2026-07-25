#include "WiFiService.h"
#include "Config.h"
#include "Secrets.h"
#include <Arduino.h>
#include <WiFi.h>

void WiFiService::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startConnection(millis());
}

void WiFiService::loop() {
  const bool connected = isConnected();
  const unsigned long nowMs = millis();

  if (connected && !wasConnected_) {
    Serial.print("WiFi connected. IP=");
    Serial.print(WiFi.localIP());
    Serial.print(", RSSI=");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else if (!connected && wasConnected_) {
    Serial.println("WiFi disconnected; background reconnect enabled");
    connectionAttempted_ = false;
  }

  wasConnected_ = connected;
  if (connected) {
    return;
  }

  if (!connectionAttempted_ ||
      nowMs - lastReconnectAttemptAtMs_ >=
          static_cast<unsigned long>(Config::WiFiReconnectIntervalMs)) {
    startConnection(nowMs);
  }
}

void WiFiService::startConnection(unsigned long nowMs) {
  lastReconnectAttemptAtMs_ = nowMs;
  connectionAttempted_ = true;

  Serial.print("WiFi connection attempt. SSID=");
  Serial.println(POC_WIFI_SSID);
  WiFi.begin(POC_WIFI_SSID, POC_WIFI_PW);
}

bool WiFiService::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

int WiFiService::rssi() const {
  return isConnected() ? WiFi.RSSI() : -127;
}
