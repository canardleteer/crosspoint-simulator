#pragma once

#include <cstdint>

// Fake USB-Serial/JTAG SOF register. Firmware HalGPIO samples
// sof_frame_index to decide a host is attached. The index never moves, so
// the host path stays on the existing SDL/USB-detect stand-in.
struct usb_serial_jtag_dev_t {
  struct {
    uint32_t sof_frame_index;
  } fram_num;
};

inline usb_serial_jtag_dev_t USB_SERIAL_JTAG{};
