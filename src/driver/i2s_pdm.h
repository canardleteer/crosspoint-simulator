#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

typedef void *i2s_chan_handle_t;

typedef enum {
  I2S_NUM_0 = 0,
  I2S_NUM_AUTO = -1,
} i2s_port_t;

typedef enum {
  I2S_ROLE_MASTER = 0,
  I2S_ROLE_SLAVE = 1,
} i2s_role_t;

typedef enum {
  I2S_DATA_BIT_WIDTH_16BIT = 16,
} i2s_data_bit_width_t;

typedef enum {
  I2S_SLOT_MODE_MONO = 0,
  I2S_SLOT_MODE_STEREO = 1,
} i2s_slot_mode_t;

typedef struct {
  i2s_port_t id;
  i2s_role_t role;
  int dma_desc_num;
  int dma_frame_num;
  bool auto_clear;
} i2s_chan_config_t;

#define I2S_CHANNEL_DEFAULT_CONFIG(port, role_)                                \
  i2s_chan_config_t {                                                          \
    (port), (role_), 6, 240, false                                             \
  }

typedef struct {
  uint32_t sample_rate_hz;
} i2s_pdm_rx_clk_config_t;

typedef struct {
  i2s_data_bit_width_t data_bit_width;
  i2s_slot_mode_t slot_mode;
} i2s_pdm_rx_slot_config_t;

typedef struct {
  gpio_num_t clk;
  gpio_num_t din;
  struct {
    bool clk_inv;
  } invert_flags;
} i2s_pdm_rx_gpio_config_t;

typedef struct {
  i2s_pdm_rx_clk_config_t clk_cfg;
  i2s_pdm_rx_slot_config_t slot_cfg;
  i2s_pdm_rx_gpio_config_t gpio_cfg;
} i2s_pdm_rx_config_t;

#define I2S_PDM_RX_CLK_DEFAULT_CONFIG(rate)                                    \
  i2s_pdm_rx_clk_config_t { (rate) }
#define I2S_PDM_RX_SLOT_DEFAULT_CONFIG(bits, mode)                             \
  i2s_pdm_rx_slot_config_t { (bits), (mode) }

inline esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *,
                                 i2s_chan_handle_t *rx) {
  if (rx)
    *rx = nullptr;
  return ESP_FAIL;
}
inline esp_err_t i2s_channel_init_pdm_rx_mode(i2s_chan_handle_t,
                                              const i2s_pdm_rx_config_t *) {
  return ESP_FAIL;
}
inline esp_err_t i2s_channel_enable(i2s_chan_handle_t) { return ESP_FAIL; }
inline esp_err_t i2s_channel_disable(i2s_chan_handle_t) { return ESP_OK; }
inline esp_err_t i2s_del_channel(i2s_chan_handle_t) { return ESP_OK; }
inline esp_err_t i2s_channel_read(i2s_chan_handle_t, void *, size_t,
                                  size_t *bytesRead, uint32_t) {
  if (bytesRead)
    *bytesRead = 0;
  return ESP_ERR_TIMEOUT;
}
