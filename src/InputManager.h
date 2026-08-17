#pragma once

#include <Arduino.h>
#include <BoardConfig.h>

#include <cstdint>

// Host InputManager follows the FreeInk SDK public surface. SDL and scripted
// contacts are fed through the ingest methods below; HalGPIO stays the event
// pump and a thin forwarder.

class InputManager {
public:
  InputManager() = default;

  void begin() {}
  uint8_t getState() const;
  void update() {}
  void beginFrame();
  void clearAll();

  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  bool isDebouncePending() const { return false; }
  unsigned long getHeldTime() const;
  unsigned long getPowerButtonHeldTime() const;

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;
  static constexpr int NUM_BUTTONS = 7;

  static constexpr int BUTTON_ADC_PIN_1 = 1;
  static constexpr int BUTTON_ADC_PIN_2 = 2;
  static constexpr int POWER_BUTTON_PIN = BoardConfig::DEFAULT_DEVICE.input.power;

  bool isPowerButtonPressed() const { return isPressed(BTN_POWER); }
  static const char *getButtonName(uint8_t buttonIndex);

  struct TouchPoint {
    bool valid = false;
    uint16_t x = 0;
    uint16_t y = 0;
    unsigned long timestamp = 0;
  };

  static constexpr uint8_t MAX_TOUCH_CONTACTS = 4;
  struct MultiTouchPoint {
    uint8_t id = 0;
    TouchPoint point{};
  };
  struct TouchSnapshot {
    uint8_t count = 0;
    uint8_t reportedCount = 0;
    bool idsStable = true;
    MultiTouchPoint points[MAX_TOUCH_CONTACTS]{};
  };

  bool hasTouch() const { return BoardConfig::hasTouch(); }
  bool supportsMultiTouch() const {
    return BoardConfig::ACTIVE.touch == BoardConfig::TouchController::Gt911;
  }
  TouchSnapshot getTouchSnapshot() const;
  TouchPoint getTouchPoint() const;
  bool isTouchPressed() const;
  bool wasTouchPressed() const;
  bool wasTouchReleased() const;
  bool wasTouchTap(float &nx, float &ny) const;
  bool wasTouchPressedAt(float &nx, float &ny) const;
  bool isTouchTapCandidate(float &nx, float &ny, unsigned long &heldMs) const;
  bool isTouchHeldAt(float &nx, float &ny) const;
  unsigned long lastTouchHeldMs() const;
  bool wasSwipe(float &nxStart, float &nyStart, float &nxEnd,
                float &nyEnd) const;
  bool wasMultiTouchSwipe(uint8_t &contactCount, float &nxStart, float &nyStart,
                          float &nxEnd, float &nyEnd,
                          unsigned long &durationMs) const;
  bool wasTouchLongPress(float &nx, float &ny) const;
  void suppressTouchContact();
  bool wasTouchActivity() const;
  bool wasHomeKeyPressed() const;
  bool wasHomeKeyTapped() const;
  bool wasHomeKeyLongPressed() const;

  using ButtonHook = uint8_t (*)();
  static void setButtonHook(ButtonHook hook) { s_buttonHook = hook; }
  static void setSharedConfirmPowerShortPressEmitsPower(bool enabled) {
    s_sharedConfirmPowerShortPressEmitsPower = enabled;
  }

  void beginAsync(uint8_t = 2, uint32_t = 15, uint8_t = 32) {}
  bool popPress(uint8_t &) { return false; }
  bool popTouchTap(float &, float &) { return false; }
  bool popSwipe(float &, float &, float &, float &) { return false; }
  bool popMultiTouchSwipe(uint8_t &, float &, float &, float &, float &,
                          unsigned long &) {
    return false;
  }

  struct ButtonAdcSample {
    int pin;
    int raw;
    int button;
  };
  void readButtonAdc(ButtonAdcSample &group1, ButtonAdcSample &group2) {
    group1 = {BUTTON_ADC_PIN_1, -1, -1};
    group2 = {BUTTON_ADC_PIN_2, -1, -1};
  }

  // Host ingest: HalGPIO owns the SDL pump and feeds already-normalized
  // panel coordinates (0..1 in the panel-native frame).
  void pressButton(uint8_t buttonIndex, unsigned long nowTicks);
  void releaseButton(uint8_t buttonIndex);
  void setButtonDown(uint8_t buttonIndex, bool down, unsigned long nowTicks);
  void beginTouchContact(uint8_t id, float nx, float ny, unsigned long nowTicks);
  void moveTouchContact(uint8_t id, float nx, float ny);
  void endTouchContact(uint8_t id, float nx, float ny, unsigned long nowTicks);
  void beginHomeKey(unsigned long nowTicks);
  void endHomeKey(unsigned long nowTicks);
  void updateHolds(unsigned long nowTicks);

private:
  static constexpr unsigned long TOUCH_TAP_SLOP_PX = 28;
  static constexpr unsigned long TOUCH_SWIPE_MIN_PX = 60;
  static constexpr unsigned long TOUCH_SWIPE_MAX_MS = 700;
  static constexpr unsigned long TOUCH_MULTI_SWIPE_MAX_MS = 2000;
  static constexpr unsigned long TOUCH_LONG_PRESS_MS = 500;
  static constexpr unsigned long HOME_KEY_LONG_PRESS_MS = 700;

  struct Contact {
    bool down = false;
    bool pressedThisFrame = false;
    bool releasedThisFrame = false;
    bool movedBeyondTapSlop = false;
    bool longPressThisFrame = false;
    bool longPressFired = false;
    bool suppressed = false;
    float startNx = 0.0f;
    float startNy = 0.0f;
    float currentNx = 0.0f;
    float currentNy = 0.0f;
    unsigned long pressedAt = 0;
    unsigned long lastHeldMs = 0;
  };

  static uint16_t panelX(float nx);
  static uint16_t panelY(float ny);
  static float panelDx(float nx0, float nx1);
  static float panelDy(float ny0, float ny1);
  Contact *contactById(uint8_t id);
  const Contact *contactById(uint8_t id) const;
  void updateContactMovement(Contact &contact, float nx, float ny);
  void refreshMultiTouchSwipe(unsigned long nowTicks);

  bool buttonDown[NUM_BUTTONS] = {};
  bool pressedThisFrame[NUM_BUTTONS] = {};
  bool releasedThisFrame[NUM_BUTTONS] = {};
  unsigned long buttonPressTime[NUM_BUTTONS] = {};

  Contact contacts[MAX_TOUCH_CONTACTS]{};
  bool activityThisFrame = false;
  bool multiTouchSwipeThisFrame = false;
  uint8_t multiTouchSwipeCount = 0;
  float multiTouchStartNx = 0.0f;
  float multiTouchStartNy = 0.0f;
  float multiTouchEndNx = 0.0f;
  float multiTouchEndNy = 0.0f;
  unsigned long multiTouchDurationMs = 0;
  unsigned long multiTouchStartedAt = 0;
  bool multiTouchTracking = false;

  unsigned long lastNowTicks = 0;

  bool homeKeyDown = false;
  bool homeKeyPressedThisFrame = false;
  bool homeKeyTappedThisFrame = false;
  bool homeKeyLongPressedThisFrame = false;
  bool homeKeyLongFired = false;
  unsigned long homeKeyPressedAt = 0;

  static ButtonHook s_buttonHook;
  static bool s_sharedConfirmPowerShortPressEmitsPower;
};
