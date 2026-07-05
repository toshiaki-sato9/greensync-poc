#include <Arduino.h>
#include <M5Unified.h>

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("===== GreenSync PoC001 Start =====");

  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.println("M5.begin done");
}

void loop() {
  Serial.println("running...");
  delay(1000);
}