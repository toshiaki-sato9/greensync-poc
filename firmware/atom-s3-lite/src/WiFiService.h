#pragma once

class WiFiService {
public:
  void begin();
  void loop();
  bool isConnected() const;
  int rssi() const;

private:
  void startConnection(unsigned long nowMs);

  unsigned long lastReconnectAttemptAtMs_ = 0;
  bool connectionAttempted_ = false;
  bool wasConnected_ = false;
};
