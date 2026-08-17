#pragma once

#include <cstdint>

// Host-safe slice of the FreeInk BoardConfig surface. Device identity is the
// same -DFREEINK_DEVICE_* contract the SDK and CrossPoint envs already use.
// This header must not overwrite flags the consuming firmware passed.

#define FREEINK_LOG_TRANSPORT_HWCDC 0
#define FREEINK_LOG_TRANSPORT_ROM_PRINTF 1
#define FREEINK_LOG_TRANSPORT_SERIAL 0
#define FREEINK_LOG_TRANSPORT_USB_CDC_WRITE 1
#ifndef FREEINK_LOG_TRANSPORT
#define FREEINK_LOG_TRANSPORT FREEINK_LOG_TRANSPORT_HWCDC
#endif

#if defined(SIMULATOR_DISPLAY_UC8179) && defined(SIMULATOR_DISPLAY_UC8279)
#error "Select at most one simulated display controller"
#endif

// Compatibility shims for older consumer inis. They may turn a flag on; they
// must not turn a firmware-supplied FREEINK_DEVICE_* off.
#if defined(SIMULATOR_DEVICE_X3)
#ifndef FREEINK_DEVICE_X3
#define FREEINK_DEVICE_X3 1
#endif
#endif
#if defined(SIMULATOR_DEVICE_X4_PRO)
#ifndef FREEINK_DEVICE_X4PRO
#define FREEINK_DEVICE_X4PRO 1
#endif
#endif
#if defined(SIMULATOR_DEVICE_STICKY)
#ifndef FREEINK_DEVICE_STICKY
#define FREEINK_DEVICE_STICKY 1
#endif
#endif

#ifndef FREEINK_DEVICE_X4
#define FREEINK_DEVICE_X4 0
#endif
#ifndef FREEINK_DEVICE_X3
#define FREEINK_DEVICE_X3 0
#endif
#ifndef FREEINK_DEVICE_X4PRO
#define FREEINK_DEVICE_X4PRO 0
#endif
#ifndef FREEINK_DEVICE_STICKY
#define FREEINK_DEVICE_STICKY 0
#endif
#ifndef FREEINK_DEVICE_PAPERMONO
#define FREEINK_DEVICE_PAPERMONO 0
#endif
#ifndef FREEINK_DEVICE_M5
#define FREEINK_DEVICE_M5 0
#endif
#ifndef FREEINK_DEVICE_MURPHY
#define FREEINK_DEVICE_MURPHY 0
#endif
#ifndef FREEINK_DEVICE_DELINK
#define FREEINK_DEVICE_DELINK 0
#endif
#ifndef FREEINK_DEVICE_LILYGO
#define FREEINK_DEVICE_LILYGO 0
#endif
#ifndef FREEINK_DEVICE_M5PAPER
#define FREEINK_DEVICE_M5PAPER 0
#endif
#ifndef FREEINK_DEVICE_PAPERS3
#define FREEINK_DEVICE_PAPERS3 0
#endif
#ifndef FREEINK_DEVICE_MURPHY_M4
#define FREEINK_DEVICE_MURPHY_M4 0
#endif

// Older sample inis omit every device flag. Default to X4 so those builds keep
// the historical 800x480 window instead of failing the SDK "no device" check.
#if !(FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4PRO ||          \
      FREEINK_DEVICE_STICKY || FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_M5 || \
      FREEINK_DEVICE_MURPHY || FREEINK_DEVICE_DELINK ||                        \
      FREEINK_DEVICE_LILYGO || FREEINK_DEVICE_M5PAPER ||                       \
      FREEINK_DEVICE_PAPERS3 || FREEINK_DEVICE_MURPHY_M4)
#undef FREEINK_DEVICE_X4
#define FREEINK_DEVICE_X4 1
#endif

#define FREEINK_MCU_C3 (FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4)
#define FREEINK_MCU_S3                                                         \
  (FREEINK_DEVICE_M5 || FREEINK_DEVICE_MURPHY || FREEINK_DEVICE_DELINK ||      \
   FREEINK_DEVICE_LILYGO || FREEINK_DEVICE_STICKY || FREEINK_DEVICE_X4PRO ||   \
   FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_PAPERS3 ||                       \
   FREEINK_DEVICE_MURPHY_M4)
#define FREEINK_MCU_ESP32 (FREEINK_DEVICE_M5PAPER)

#ifndef FREEINK_CAP_TOUCH
#define FREEINK_CAP_TOUCH                                                      \
  (FREEINK_DEVICE_STICKY || FREEINK_DEVICE_X4PRO ||                            \
   FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_M5PAPER ||                       \
   FREEINK_DEVICE_LILYGO || FREEINK_DEVICE_MURPHY ||                           \
   FREEINK_DEVICE_PAPERS3 || FREEINK_DEVICE_MURPHY_M4)
#endif
#ifndef FREEINK_CAP_FRONTLIGHT
#define FREEINK_CAP_FRONTLIGHT                                                 \
  (FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_PAPERMONO ||                         \
   FREEINK_DEVICE_DELINK || FREEINK_DEVICE_MURPHY || FREEINK_DEVICE_LILYGO ||  \
   FREEINK_DEVICE_MURPHY_M4)
#endif
#ifndef FREEINK_CAP_WARMLIGHT
#define FREEINK_CAP_WARMLIGHT (FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4)
#endif
#ifndef FREEINK_CAP_USB_MSC
#define FREEINK_CAP_USB_MSC 0
#endif
#ifndef FREEINK_CAP_BLE_HID_HOST
#define FREEINK_CAP_BLE_HID_HOST 0
#endif
#ifndef FREEINK_CAP_BLE_KEYBOARD
#define FREEINK_CAP_BLE_KEYBOARD FREEINK_CAP_BLE_HID_HOST
#endif
#ifndef FREEINK_CAP_COLOR
#define FREEINK_CAP_COLOR (FREEINK_DEVICE_M5)
#endif
#ifndef FREEINK_CAP_AUDIO
#define FREEINK_CAP_AUDIO (FREEINK_DEVICE_MURPHY || FREEINK_DEVICE_M5)
#endif
#ifndef FREEINK_CAP_MIC
#define FREEINK_CAP_MIC (FREEINK_DEVICE_STICKY || FREEINK_DEVICE_PAPERMONO)
#endif
#ifndef FREEINK_CAP_RTC
#define FREEINK_CAP_RTC                                                        \
  (FREEINK_DEVICE_X3 || FREEINK_DEVICE_STICKY || FREEINK_DEVICE_X4PRO ||       \
   FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_PAPERS3)
#endif
#ifndef FREEINK_CAP_TEMP_HUMIDITY
#define FREEINK_CAP_TEMP_HUMIDITY (FREEINK_DEVICE_STICKY)
#endif
#ifndef FREEINK_CAP_IMU
#define FREEINK_CAP_IMU (FREEINK_DEVICE_X3 || FREEINK_DEVICE_STICKY)
#endif
#ifndef FREEINK_CAP_BUZZER
#define FREEINK_CAP_BUZZER                                                     \
  (FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY ||                           \
   FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_PAPERS3)
#endif
#ifndef FREEINK_CAP_LED
#define FREEINK_CAP_LED (FREEINK_DEVICE_M5 || FREEINK_DEVICE_PAPERMONO)
#endif
#ifndef FREEINK_FB_PSRAM
#define FREEINK_FB_PSRAM (FREEINK_DEVICE_M5PAPER || FREEINK_DEVICE_PAPERMONO)
#endif
#ifndef FREEINK_SD_SDMMC
#define FREEINK_SD_SDMMC                                                       \
  (FREEINK_DEVICE_DELINK || FREEINK_DEVICE_X4PRO ||                            \
   FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_MURPHY_M4)
#endif

#if defined(SIMULATOR_DEVICE_X3) && defined(SIMULATOR_DISPLAY_UC8179)
#error "Xteink X3 revisions use UC8253 or UC8279d, not UC8179"
#endif
#if defined(SIMULATOR_DEVICE_STICKY) &&                                         \
    (defined(SIMULATOR_DISPLAY_UC8179) || defined(SIMULATOR_DISPLAY_UC8279))
#error "Seeed Sticky uses SSD1677; do not select an Xteink controller override"
#endif

namespace BoardConfig {

enum class Board {
  XteinkX4,
  XteinkX3,
  XteinkX3Uc8279,
  XteinkX4Pro,
  Sticky,
  PaperMono,
};

enum class DisplayController {
  SSD1677,
  UC8253,
  UC8279,
  UC8179,
};

enum class TouchController : uint8_t {
  None,
  Gt911,
  Ft5x06,
};

enum class FrontlightStyle : uint8_t {
  None,
  Pwm,
  PwmWarm,
  PmicPwm,
};

struct ViewableInsets {
  uint8_t top = 9;
  uint8_t right = 3;
  uint8_t bottom = 3;
  uint8_t left = 3;
};

struct BoardProfile {
  Board board;
  const char *name;
  DisplayController displayController;
  uint8_t displayControllerVariant;
  uint16_t displayWidth;
  uint16_t displayHeight;
  struct {
    int8_t up;
    int8_t down;
    int8_t power;
  } input;
  ViewableInsets viewableInsets = {};
  float uiScale = 1.0f;
  TouchController touch = TouchController::None;
  bool hasHomeKey = false;
  FrontlightStyle frontlight = FrontlightStyle::None;
};

#if defined(SIMULATOR_DISPLAY_UC8179)
inline constexpr DisplayController X4_DISPLAY_CONTROLLER =
    DisplayController::UC8179;
inline constexpr uint8_t X4_DISPLAY_CONTROLLER_VARIANT = 0x01;
#elif defined(SIMULATOR_DISPLAY_UC8279)
inline constexpr DisplayController X4_DISPLAY_CONTROLLER =
    DisplayController::UC8279;
inline constexpr uint8_t X4_DISPLAY_CONTROLLER_VARIANT = 0x68;
#else
inline constexpr DisplayController X4_DISPLAY_CONTROLLER =
    DisplayController::SSD1677;
inline constexpr uint8_t X4_DISPLAY_CONTROLLER_VARIANT = 0;
#endif

inline constexpr ViewableInsets X4_INSETS = {9, 3, 3, 3};
inline constexpr ViewableInsets X4PRO_INSETS = {9, 7, 3, 7};

inline constexpr BoardProfile XTEINK_X4 = {
    Board::XteinkX4,
    "xteink_x4",
    X4_DISPLAY_CONTROLLER,
    X4_DISPLAY_CONTROLLER_VARIANT,
    800,
    480,
    {4, 5, 9},
    X4_INSETS,
    1.0f,
    TouchController::None,
    false,
    FrontlightStyle::None,
};
inline constexpr BoardProfile XTEINK_X3 = {
    Board::XteinkX3,
    "xteink_x3",
    DisplayController::UC8253,
    0,
    792,
    528,
    {4, 5, 9},
    X4_INSETS,
    1.0f,
    TouchController::None,
    false,
    FrontlightStyle::None,
};
inline constexpr BoardProfile XTEINK_X3_UC8279 = {
    Board::XteinkX3Uc8279,
    "xteink_x3_uc8279",
    DisplayController::UC8279,
    0,
    792,
    528,
    {4, 5, 9},
    X4_INSETS,
    1.0f,
    TouchController::None,
    false,
    FrontlightStyle::None,
};
inline constexpr BoardProfile XTEINK_X4_PRO = {
    Board::XteinkX4Pro,
    "xteink_x4_pro",
    X4_DISPLAY_CONTROLLER,
    X4_DISPLAY_CONTROLLER_VARIANT,
    800,
    480,
    {0, 7, 1},
    X4PRO_INSETS,
    1.2f,
    TouchController::Gt911,
    true,
    FrontlightStyle::PwmWarm,
};
inline constexpr BoardProfile STICKY = {
    Board::Sticky,
    "sticky",
    DisplayController::SSD1677,
    0,
    800,
    480,
    {5, 6, 0},
    X4_INSETS,
    1.2f,
    TouchController::Gt911,
    false,
    FrontlightStyle::None,
};
inline constexpr BoardProfile PAPER_MONO = {
    Board::PaperMono,
    "m5stack_paper_mono",
    DisplayController::SSD1677,
    0,
    800,
    480,
    {2, 3, 0},
    X4PRO_INSETS,
    1.0f,
    TouchController::Ft5x06,
    false,
    FrontlightStyle::PmicPwm,
};

constexpr uint16_t cmax16(uint16_t a, uint16_t b) { return a > b ? a : b; }
constexpr uint32_t cmax32(uint32_t a, uint32_t b) { return a > b ? a : b; }
constexpr uint32_t panelBytes(const BoardProfile &p) {
  return static_cast<uint32_t>(p.displayWidth / 8) * p.displayHeight;
}

constexpr uint16_t MAX_DISPLAY_WIDTH = cmax16(
    cmax16(FREEINK_DEVICE_X4 ? XTEINK_X4.displayWidth : 0,
           FREEINK_DEVICE_X3 ? XTEINK_X3.displayWidth : 0),
    cmax16(cmax16(FREEINK_DEVICE_X4PRO ? XTEINK_X4_PRO.displayWidth : 0,
                  FREEINK_DEVICE_STICKY ? STICKY.displayWidth : 0),
           FREEINK_DEVICE_PAPERMONO ? PAPER_MONO.displayWidth : 0));
constexpr uint16_t MAX_DISPLAY_HEIGHT = cmax16(
    cmax16(FREEINK_DEVICE_X4 ? XTEINK_X4.displayHeight : 0,
           FREEINK_DEVICE_X3 ? XTEINK_X3.displayHeight : 0),
    cmax16(cmax16(FREEINK_DEVICE_X4PRO ? XTEINK_X4_PRO.displayHeight : 0,
                  FREEINK_DEVICE_STICKY ? STICKY.displayHeight : 0),
           FREEINK_DEVICE_PAPERMONO ? PAPER_MONO.displayHeight : 0));
constexpr uint32_t MAX_FRAMEBUFFER_BYTES = cmax32(
    cmax32(FREEINK_DEVICE_X4 ? panelBytes(XTEINK_X4) : 0,
           FREEINK_DEVICE_X3 ? panelBytes(XTEINK_X3) : 0),
    cmax32(cmax32(FREEINK_DEVICE_X4PRO ? panelBytes(XTEINK_X4_PRO) : 0,
                  FREEINK_DEVICE_STICKY ? panelBytes(STICKY) : 0),
           FREEINK_DEVICE_PAPERMONO ? panelBytes(PAPER_MONO) : 0));

#if FREEINK_DEVICE_PAPERMONO &&                                                \
    !(FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4PRO ||        \
      FREEINK_DEVICE_STICKY)
inline constexpr BoardProfile DEFAULT_DEVICE = PAPER_MONO;
#elif FREEINK_DEVICE_STICKY &&                                                 \
    !(FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3 || FREEINK_DEVICE_X4PRO ||        \
      FREEINK_DEVICE_PAPERMONO)
inline constexpr BoardProfile DEFAULT_DEVICE = STICKY;
#elif FREEINK_DEVICE_X4PRO &&                                                  \
    !(FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3 || FREEINK_DEVICE_STICKY ||       \
      FREEINK_DEVICE_PAPERMONO)
inline constexpr BoardProfile DEFAULT_DEVICE = XTEINK_X4_PRO;
#elif FREEINK_DEVICE_X3 && !FREEINK_DEVICE_X4
#if defined(SIMULATOR_DISPLAY_UC8279)
inline constexpr BoardProfile DEFAULT_DEVICE = XTEINK_X3_UC8279;
#else
inline constexpr BoardProfile DEFAULT_DEVICE = XTEINK_X3;
#endif
#else
inline constexpr BoardProfile DEFAULT_DEVICE = XTEINK_X4;
#endif

inline BoardProfile ACTIVE = DEFAULT_DEVICE;

inline bool selectDevice(Board board) {
  switch (board) {
#if FREEINK_DEVICE_X4
  case Board::XteinkX4:
    ACTIVE = XTEINK_X4;
    return true;
#endif
#if FREEINK_DEVICE_X3
  case Board::XteinkX3:
    ACTIVE = XTEINK_X3;
    return true;
  case Board::XteinkX3Uc8279:
    ACTIVE = XTEINK_X3_UC8279;
    return true;
#endif
#if FREEINK_DEVICE_X4PRO
  case Board::XteinkX4Pro:
    ACTIVE = XTEINK_X4_PRO;
    return true;
#endif
#if FREEINK_DEVICE_STICKY
  case Board::Sticky:
    ACTIVE = STICKY;
    return true;
#endif
#if FREEINK_DEVICE_PAPERMONO
  case Board::PaperMono:
    ACTIVE = PAPER_MONO;
    return true;
#endif
  default:
    return false;
  }
}

inline bool isX4Pro() { return ACTIVE.board == Board::XteinkX4Pro; }
inline bool isSticky() { return ACTIVE.board == Board::Sticky; }
inline bool isPaperMono() { return ACTIVE.board == Board::PaperMono; }
inline bool hasTouch() { return ACTIVE.touch != TouchController::None; }
inline bool hasHomeKey() { return ACTIVE.hasHomeKey; }
inline bool hasPwmFrontlight() {
  return ACTIVE.frontlight == FrontlightStyle::Pwm ||
         ACTIVE.frontlight == FrontlightStyle::PwmWarm;
}
inline bool hasFrontlight() {
  return ACTIVE.frontlight != FrontlightStyle::None;
}
inline bool hasColorTemperature() {
  return ACTIVE.frontlight == FrontlightStyle::PwmWarm;
}

inline void holdPowerRails() {}

} // namespace BoardConfig
