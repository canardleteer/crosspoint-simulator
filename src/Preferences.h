#pragma once

#include <cstdint>
#include <map>
#include <string>

// Arduino Preferences over host memory. No flash/NVS physics; enough for
// firmware HalGPIO's cphw / dev_ovr keys once that TU compiles here.
class Preferences {
public:
  bool begin(const char *name, bool /*readOnly*/ = false,
             const char * /*partition*/ = nullptr) {
    if (!name || *name == '\0')
      return false;
    ns_ = name;
    open_ = true;
    return true;
  }

  void end() {
    open_ = false;
    ns_.clear();
  }

  uint8_t getUChar(const char *key, uint8_t defaultValue = 0) const {
    if (!open_ || !key)
      return defaultValue;
    const auto it = store().find(slot(key));
    return it == store().end() ? defaultValue : it->second;
  }

  size_t putUChar(const char *key, uint8_t value) {
    if (!open_ || !key)
      return 0;
    store()[slot(key)] = value;
    return sizeof(value);
  }

private:
  std::string slot(const char *key) const { return ns_ + "/" + key; }

  static std::map<std::string, uint8_t> &store() {
    static std::map<std::string, uint8_t> values;
    return values;
  }

  std::string ns_;
  bool open_ = false;
};
