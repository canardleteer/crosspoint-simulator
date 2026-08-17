#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

typedef struct {
  int slot;
  int max_freq_khz;
  uint32_t flags;
} sdmmc_host_t;

typedef struct {
  int width;
  gpio_num_t clk;
  gpio_num_t cmd;
  gpio_num_t d0;
  gpio_num_t d1;
  gpio_num_t d2;
  gpio_num_t d3;
  uint32_t flags;
} sdmmc_slot_config_t;

#define SDMMC_FREQ_DEFAULT 40000
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP (1u << 0)

#define SDMMC_HOST_DEFAULT()                                                   \
  sdmmc_host_t { 1, SDMMC_FREQ_DEFAULT, 0 }
#define SDMMC_SLOT_CONFIG_DEFAULT()                                            \
  sdmmc_slot_config_t {                                                        \
    1, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC,        \
        GPIO_NUM_NC, 0                                                         \
  }

inline esp_err_t sdmmc_host_init() { return ESP_FAIL; }
inline esp_err_t sdmmc_host_init_slot(int, const sdmmc_slot_config_t *) {
  return ESP_FAIL;
}
inline esp_err_t sdmmc_host_deinit() { return ESP_OK; }
