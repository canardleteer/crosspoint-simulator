#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/sdmmc_host.h"
#include "esp_err.h"

typedef struct {
  struct {
    uint32_t capacity;
  } csd;
} sdmmc_card_t;

inline esp_err_t sdmmc_card_init(const sdmmc_host_t *, sdmmc_card_t *) {
  return ESP_FAIL;
}
inline esp_err_t sdmmc_read_sectors(sdmmc_card_t *, void *, size_t, size_t) {
  return ESP_FAIL;
}
inline esp_err_t sdmmc_write_sectors(sdmmc_card_t *, const void *, size_t,
                                     size_t) {
  return ESP_FAIL;
}
