#pragma once

class WiFiService {
public:
  void begin();
  bool isConnected() const;
  int rssi() const;
};