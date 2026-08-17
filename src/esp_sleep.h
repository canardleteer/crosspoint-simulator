#pragma once

#include <cstdint>

#include "esp_err.h"

// Host deep sleep stays a process relaunch in HalPowerManager. These names
// exist so firmware GPIO/power HAL can include the ESP-IDF header.
typedef enum {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_ALL,
  ESP_SLEEP_WAKEUP_EXT0,
  ESP_SLEEP_WAKEUP_EXT1,
  ESP_SLEEP_WAKEUP_TIMER,
  ESP_SLEEP_WAKEUP_TOUCHPAD,
  ESP_SLEEP_WAKEUP_ULP,
  ESP_SLEEP_WAKEUP_GPIO,
  ESP_SLEEP_WAKEUP_UART,
} esp_sleep_source_t;

typedef esp_sleep_source_t esp_sleep_wakeup_cause_t;

inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() {
  return ESP_SLEEP_WAKEUP_UNDEFINED;
}

inline esp_err_t esp_sleep_enable_timer_wakeup(uint64_t) { return ESP_OK; }
inline esp_err_t esp_sleep_enable_gpio_wakeup() { return ESP_OK; }
inline esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t) {
  return ESP_OK;
}
