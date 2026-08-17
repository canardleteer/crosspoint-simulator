#pragma once

#include <Arduino.h>
#include <BoardConfig.h>

#include <cstdint>

namespace freeink {

class Imu {
public:
  struct Sample {
    float ax = 0;
    float ay = 0;
    float az = 0;
    float gx = 0;
    float gy = 0;
    float gz = 0;
  };

  bool begin() {
    begun_ = FREEINK_CAP_IMU;
    return begun_;
  }
  bool present() const { return begun_; }
  bool read(Sample &) { return false; }
  bool sleep() { return begun_; }
  bool wake() { return begun_; }

private:
  bool begun_ = false;
};

} // namespace freeink

using Imu = freeink::Imu;
