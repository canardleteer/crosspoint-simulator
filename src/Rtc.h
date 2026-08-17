#pragma once

#include <Arduino.h>
#include <BoardConfig.h>

#include <cstdint>
#include <ctime>

namespace freeink {

class Rtc {
public:
  struct DateTime {
    uint16_t year = 2000;
    uint8_t month = 1;
    uint8_t day = 1;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
  };

  bool begin() {
    begun_ = FREEINK_CAP_RTC;
    return begun_;
  }
  bool present() const { return begun_; }

  bool now(DateTime &out) {
    if (!begun_)
      return false;
    if (hasOverride_) {
      out = override_;
      return true;
    }
    const std::time_t t = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    out.year = static_cast<uint16_t>(utc.tm_year + 1900);
    out.month = static_cast<uint8_t>(utc.tm_mon + 1);
    out.day = static_cast<uint8_t>(utc.tm_mday);
    out.hour = static_cast<uint8_t>(utc.tm_hour);
    out.minute = static_cast<uint8_t>(utc.tm_min);
    out.second = static_cast<uint8_t>(utc.tm_sec);
    out.weekday = static_cast<uint8_t>(utc.tm_wday);
    return true;
  }

  bool set(const DateTime &dt) {
    if (!begun_)
      return false;
    override_ = dt;
    hasOverride_ = true;
    return true;
  }

  bool adjust(int32_t, DateTime *out = nullptr) {
    if (!begun_)
      return false;
    if (out)
      return now(*out);
    return true;
  }

private:
  bool begun_ = false;
  bool hasOverride_ = false;
  DateTime override_{};
};

} // namespace freeink

using Rtc = freeink::Rtc;
