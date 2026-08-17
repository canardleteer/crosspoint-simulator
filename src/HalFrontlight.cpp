#include "HalFrontlight.h"
#include "sim_host_log.h"

#include <BoardConfig.h>

#include <algorithm>

HalFrontlight &HalFrontlight::getInstance() {
  static HalFrontlight instance;
  return instance;
}

void HalFrontlight::begin(uint8_t brightness, uint8_t warmth, bool on) {
  if (!present())
    return;
  lastBrightness = std::min<uint8_t>(brightness, 100);
  lastWarmth = std::min<uint8_t>(warmth, 100);
  lit = on;
  simHostLog(SIM_HOST_INFO, "frontlight",
             "[SIM] X4 Pro frontlight: %s, brightness=%u%%, warmth=%u%%\n",
             lit ? "on" : "off", static_cast<unsigned>(lastBrightness),
             static_cast<unsigned>(lastWarmth));
}

bool HalFrontlight::present() const { return BoardConfig::isX4Pro(); }

bool HalFrontlight::hasColorTemperature() const {
  return BoardConfig::isX4Pro();
}

void HalFrontlight::setBrightness(uint8_t percent) {
  lastBrightness = std::min<uint8_t>(percent, 100);
}

void HalFrontlight::setWarmth(uint8_t warmPercent) {
  lastWarmth = std::min<uint8_t>(warmPercent, 100);
}

void HalFrontlight::setOn(bool on) { lit = present() && on; }

uint8_t HalFrontlight::brightness() const { return lastBrightness; }

uint8_t HalFrontlight::warmth() const { return lastWarmth; }

bool HalFrontlight::isOn() const { return lit; }
