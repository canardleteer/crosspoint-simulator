#pragma once

#include <cstddef>
#include <cstdint>

// No-op I2C. beginTransmission / requestFrom fail so firmware probes do not
// invent a bus or a gauge.
class TwoWire {
public:
  void begin(int = -1, int = -1, uint32_t = 0) {}
  void beginTransmission(uint8_t) {}
  size_t write(uint8_t) { return 1; }
  uint8_t endTransmission(bool = true) { return 1; }
  uint8_t requestFrom(uint8_t, uint8_t, uint8_t = 1) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
};

inline TwoWire Wire;
