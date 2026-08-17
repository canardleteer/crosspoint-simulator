#pragma once

// Empty Paper Mono PMIC surface. Firmware HalPowerManager asks
// requestShutdown() before deep sleep; returning false leaves host relaunch
// as the only sleep path.
namespace freeink {
namespace m5pm1 {

inline bool requestShutdown() { return false; }

} // namespace m5pm1
} // namespace freeink
