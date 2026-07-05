#include <Arduino.h>
#include <WiFi.h>
#include <M5Unified.h>
#include "secrets.h"

const char* WIFI_SSID = POC_WIFI_SSID;
const char* WIFI_PASS = POC_WIFI_PW;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("===== GreenSync PoC001 WiFi Test =====");

  auto cfg = M5.config();
  M5.begin(cfg);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("WiFi connection failed.");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK, RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("WiFi disconnected.");
  }

  delay(5000);
}