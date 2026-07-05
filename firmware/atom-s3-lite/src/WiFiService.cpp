#include "WiFiService.h"
#include "Secrets.h"
#include <Arduino.h>
#include <WiFi.h>

void WiFiService::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(POC_WIFI_SSID, POC_WIFI_PW);

  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.print("WiFi connected. IP=");
  Serial.println(WiFi.localIP());
}

bool WiFiService::isConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

int WiFiService::rssi() const {
  return WiFi.RSSI();
}