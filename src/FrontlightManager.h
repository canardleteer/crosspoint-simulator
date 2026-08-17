#pragma once

#include <Arduino.h>
#include <BoardConfig.h>

// Host FrontlightManager follows the FreeInk include name. PWM is not
// modeled; brightness and warmth are state only so firmware HalFrontlight
// can compile against this header.
class FrontlightManager {
public:
  void begin() { begun_ = present(); }

  void setBrightness(uint8_t percent) {
    brightness_ = percent > 100 ? 100 : percent;
  }
  void setBrightnessLevel(uint8_t level) { brightnessLevel_ = level; }
  void off() { setBrightness(0); }
  void on() { setBrightness(brightness_ == 0 ? 50 : brightness_); }
  void setColorTemperature(uint8_t warmPercent) {
    warmPercent_ = warmPercent > 100 ? 100 : warmPercent;
  }

  bool present() const { return BoardConfig::hasFrontlight(); }
  bool hasColorTemperature() const { return BoardConfig::hasColorTemperature(); }

  uint8_t brightness() const { return brightness_; }
  uint8_t brightnessLevel() const { return brightnessLevel_; }
  uint8_t colorTemperature() const { return warmPercent_; }

private:
  bool begun_ = false;
  uint8_t brightness_ = 0;
  uint8_t brightnessLevel_ = 0;
  uint8_t warmPercent_ = 50;
};
