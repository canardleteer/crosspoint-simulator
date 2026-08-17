#include "HalGPIO.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SimulatorDisplay.h"
#include "SimulatorLifecycle.h"

extern GfxRenderer renderer;

// Keyboard mapping:
//   BTN_BACK    (0) → Escape
//   BTN_CONFIRM (1) → Return
//   BTN_LEFT    (2) → Left arrow
//   BTN_RIGHT   (3) → Right arrow
//   BTN_UP      (4) → Up arrow
//   BTN_DOWN    (5) → Down arrow
//   BTN_POWER   (6) → P
//   Simulator sleep shortcut → S

static constexpr int NUM_BUTTONS = InputManager::NUM_BUTTONS;
static constexpr SDL_Scancode SIMULATOR_SLEEP_SCANCODE = SDL_SCANCODE_S;
static constexpr SDL_Scancode HOME_KEY_SCANCODE = SDL_SCANCODE_H;

static const SDL_Scancode buttonScancode[NUM_BUTTONS] = {
    SDL_SCANCODE_ESCAPE, // BTN_BACK
    SDL_SCANCODE_RETURN, // BTN_CONFIRM
    SDL_SCANCODE_LEFT,   // BTN_LEFT
    SDL_SCANCODE_RIGHT,  // BTN_RIGHT
    SDL_SCANCODE_UP,     // BTN_UP
    SDL_SCANCODE_DOWN,   // BTN_DOWN
    SDL_SCANCODE_P,      // BTN_POWER
};

static bool simulatorSleepRequested = false;
static uint8_t mouseContactId = 0;
static bool mouseDown = false;
static InputManager *g_input = nullptr;

namespace {

enum class SyntheticAction {
  KeyDown,
  KeyUp,
  TouchDown,
  TouchUp,
  HomeDown,
  HomeUp,
  Sleep,
  Quit
};

struct SyntheticEvent {
  unsigned long atMs;
  SyntheticAction action;
  int button = -1;
  float logicalNx = 0.0f;
  float logicalNy = 0.0f;
  uint8_t contactId = 0;
  bool handled = false;
};

std::vector<SyntheticEvent> syntheticEvents;
bool syntheticEventsInitialized = false;

float clamp01(float value) { return std::max(0.0f, std::min(1.0f, value)); }

uint16_t panelWidth() { return BoardConfig::ACTIVE.displayWidth; }
uint16_t panelHeight() { return BoardConfig::ACTIVE.displayHeight; }

void logicalToPanelNormalized(float logicalNx, float logicalNy, float &panelNx,
                              float &panelNy) {
  const int logicalWidth = renderer.getScreenWidth();
  const int logicalHeight = renderer.getScreenHeight();
  const int lx = static_cast<int>(clamp01(logicalNx) *
                                  static_cast<float>(logicalWidth - 1));
  const int ly = static_cast<int>(clamp01(logicalNy) *
                                  static_cast<float>(logicalHeight - 1));

  int physicalX = 0;
  int physicalY = 0;
  switch (renderer.getOrientation()) {
  case GfxRenderer::Portrait:
    physicalX = ly;
    physicalY = panelHeight() - 1 - lx;
    break;
  case GfxRenderer::PortraitInverted:
    physicalX = panelWidth() - 1 - ly;
    physicalY = lx;
    break;
  case GfxRenderer::LandscapeClockwise:
    physicalX = panelWidth() - 1 - lx;
    physicalY = panelHeight() - 1 - ly;
    break;
  case GfxRenderer::LandscapeCounterClockwise:
  default:
    physicalX = lx;
    physicalY = ly;
    break;
  }

  panelNx = clamp01(static_cast<float>(physicalX) /
                    static_cast<float>(std::max(1, panelWidth() - 1)));
  panelNy = clamp01(static_cast<float>(physicalY) /
                    static_cast<float>(std::max(1, panelHeight() - 1)));
}

InputManager &input() { return *g_input; }

void beginTouch(float logicalNx, float logicalNy, uint8_t contactId) {
  float panelNx = 0.0f;
  float panelNy = 0.0f;
  logicalToPanelNormalized(logicalNx, logicalNy, panelNx, panelNy);
  input().beginTouchContact(contactId, panelNx, panelNy, SDL_GetTicks());
}

void moveTouch(float logicalNx, float logicalNy, uint8_t contactId) {
  float panelNx = 0.0f;
  float panelNy = 0.0f;
  logicalToPanelNormalized(logicalNx, logicalNy, panelNx, panelNy);
  input().moveTouchContact(contactId, panelNx, panelNy);
}

void endTouch(float logicalNx, float logicalNy, uint8_t contactId) {
  float panelNx = 0.0f;
  float panelNy = 0.0f;
  logicalToPanelNormalized(logicalNx, logicalNy, panelNx, panelNy);
  input().endTouchContact(contactId, panelNx, panelNy, SDL_GetTicks());
}

bool parseTouchSpec(const std::string &detail, float &x1, float &y1, float &x2,
                    float &y2, unsigned long &duration, bool swipe) {
  unsigned parsedDuration = swipe ? 250 : 80;
  int parsed = 0;
  if (swipe) {
    parsed = std::sscanf(detail.c_str(), "%f,%f,%f,%f,%u", &x1, &y1, &x2, &y2,
                         &parsedDuration);
    if (parsed < 4)
      return false;
  } else {
    parsed = std::sscanf(detail.c_str(), "%f,%f,%u", &x1, &y1, &parsedDuration);
    if (parsed < 2)
      return false;
    x2 = x1;
    y2 = y1;
  }

  // Scripts normally use logical display pixels because those coordinates are
  // easy to read from UI layouts and screenshots. Preserve support for the
  // earlier 0.0-1.0 normalized form so existing local QA scripts keep working.
  const auto normalize = [](float value, int extent) {
    if (value >= 0.0f && value <= 1.0f)
      return value;
    return clamp01(value / static_cast<float>(std::max(1, extent - 1)));
  };
  const int logicalWidth = renderer.getScreenWidth();
  const int logicalHeight = renderer.getScreenHeight();
  x1 = normalize(x1, logicalWidth);
  y1 = normalize(y1, logicalHeight);
  x2 = normalize(x2, logicalWidth);
  y2 = normalize(y2, logicalHeight);
  duration = parsedDuration;
  return true;
}

void requestSimulatorSleep() {
  simulatorSleepRequested = true;
  // Current CrossPoint firmware sleeps on a held physical power button. Keep
  // the compatibility latch above for older consumers, and also drive the
  // current public HalGPIO state so the S shortcut follows the real firmware
  // sleep path.
  input().pressButton(HalGPIO::BTN_POWER, SDL_GetTicks());
}

std::string uppercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

int namedButton(const std::string &name) {
  if (name == "ESCAPE" || name == "BACK")
    return HalGPIO::BTN_BACK;
  if (name == "RETURN" || name == "ENTER" || name == "CONFIRM")
    return HalGPIO::BTN_CONFIRM;
  if (name == "LEFT")
    return HalGPIO::BTN_LEFT;
  if (name == "RIGHT")
    return HalGPIO::BTN_RIGHT;
  if (name == "UP")
    return HalGPIO::BTN_UP;
  if (name == "DOWN")
    return HalGPIO::BTN_DOWN;
  if (name == "P" || name == "POWER")
    return HalGPIO::BTN_POWER;
  return -1;
}

void initializeSyntheticEvents() {
  if (syntheticEventsInitialized)
    return;
  syntheticEventsInitialized = true;

  const char *script = std::getenv("CROSSPOINT_SIM_INPUT_SCRIPT");
  if (!script || script[0] == '\0')
    return;

  const std::string spec(script);
  size_t start = 0;
  while (start < spec.size()) {
    const size_t end = spec.find(';', start);
    const std::string item = spec.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    const size_t firstColon = item.find(':');
    const size_t secondColon = firstColon == std::string::npos
                                   ? std::string::npos
                                   : item.find(':', firstColon + 1);
    if (firstColon != std::string::npos) {
      const unsigned long atMs =
          std::strtoul(item.substr(0, firstColon).c_str(), nullptr, 10);
      const std::string key = uppercase(
          item.substr(firstColon + 1, secondColon == std::string::npos
                                          ? std::string::npos
                                          : secondColon - firstColon - 1));
      if (key == "QUIT") {
        syntheticEvents.push_back({atMs, SyntheticAction::Quit});
      } else if (key == "S" || key == "SLEEP") {
        syntheticEvents.push_back({atMs, SyntheticAction::Sleep});
      } else if (key == "HOME") {
        const unsigned long holdMs =
            secondColon == std::string::npos
                ? 80
                : std::strtoul(item.substr(secondColon + 1).c_str(), nullptr,
                               10);
        syntheticEvents.push_back({atMs, SyntheticAction::HomeDown});
        syntheticEvents.push_back({atMs + holdMs, SyntheticAction::HomeUp});
      } else if ((key == "TAP" || key == "SWIPE" || key == "TAP2" ||
                  key == "SWIPE2") &&
                 secondColon != std::string::npos) {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        unsigned long duration = 0;
        const bool swipe = key == "SWIPE" || key == "SWIPE2";
        const uint8_t contactId = (key == "TAP2" || key == "SWIPE2") ? 1 : 0;
        if (parseTouchSpec(item.substr(secondColon + 1), x1, y1, x2, y2,
                           duration, swipe)) {
          syntheticEvents.push_back(
              {atMs, SyntheticAction::TouchDown, -1, x1, y1, contactId});
          syntheticEvents.push_back({atMs + duration, SyntheticAction::TouchUp,
                                     -1, x2, y2, contactId});
        }
      } else {
        const int button = namedButton(key);
        if (button >= 0) {
          const unsigned long holdMs =
              secondColon == std::string::npos
                  ? 80
                  : std::strtoul(item.substr(secondColon + 1).c_str(), nullptr,
                                 10);
          syntheticEvents.push_back({atMs, SyntheticAction::KeyDown, button});
          syntheticEvents.push_back(
              {atMs + holdMs, SyntheticAction::KeyUp, button});
        }
      }
    }

    if (end == std::string::npos)
      break;
    start = end + 1;
  }

  std::sort(syntheticEvents.begin(), syntheticEvents.end(),
            [](const SyntheticEvent &a, const SyntheticEvent &b) {
              return a.atMs < b.atMs;
            });
}

void processSyntheticEvents() {
  initializeSyntheticEvents();
  const unsigned long now = millis();
  for (auto &event : syntheticEvents) {
    if (event.handled || event.atMs > now)
      continue;
    event.handled = true;
    switch (event.action) {
    case SyntheticAction::KeyDown:
      // Held-time calculations use SDL_GetTicks() for real keyboard events;
      // synthetic presses must use the same clock origin to avoid unsigned
      // underflow being mistaken for an immediate long press.
      input().pressButton(static_cast<uint8_t>(event.button), SDL_GetTicks());
      break;
    case SyntheticAction::KeyUp:
      input().releaseButton(static_cast<uint8_t>(event.button));
      break;
    case SyntheticAction::TouchDown:
      beginTouch(event.logicalNx, event.logicalNy, event.contactId);
      break;
    case SyntheticAction::TouchUp:
      endTouch(event.logicalNx, event.logicalNy, event.contactId);
      break;
    case SyntheticAction::HomeDown:
      input().beginHomeKey(SDL_GetTicks());
      break;
    case SyntheticAction::HomeUp:
      input().endHomeKey(SDL_GetTicks());
      break;
    case SyntheticAction::Sleep:
      requestSimulatorSleep();
      break;
    case SyntheticAction::Quit:
      SimulatorDisplay::requestQuit();
      break;
    }
  }
}

uint8_t mouseChordContactId() {
  if (!input().supportsMultiTouch())
    return 0;
  const uint8_t *state = SDL_GetKeyboardState(nullptr);
  if (state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT])
    return 1;
  return 0;
}

} // namespace

static int scancodeToButton(SDL_Scancode sc) {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (buttonScancode[i] == sc)
      return i;
  }
  return -1;
}

void HalGPIO::begin() {
  // ACTIVE is already the compile-set default. The X3/X4 discriminator is
  // only meaningful for the shared C3 binary; other boards stay on X4.
  const auto board = BoardConfig::ACTIVE.board;
  _deviceType = (board == BoardConfig::Board::XteinkX3 ||
                 board == BoardConfig::Board::XteinkX3Uc8279)
                    ? DeviceType::X3
                    : DeviceType::X4;
  g_input = &inputMgr;
  inputMgr.begin();
}

bool HalGPIO::isXteinkDevice() const {
  // Match the firmware helper's narrower meaning: the runtime-detected C3
  // X3/X4 pair. X4 Pro is an Xteink product but uses its own S3 board profile.
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4;
}

bool HalGPIO::hasEdgeSideButtons() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX3Uc8279 ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::XteinkX4Pro;
}

void HalGPIO::beginFrame() {
  // Clear the press/release edge latches once per frame. See update() for why
  // this is deliberately separate from the SDL poll.
  inputMgr.beginFrame();
}

void HalGPIO::update() {
  g_input = &inputMgr;
  // Per-frame press/release edges are intentionally NOT cleared here; that
  // happens once per frame in beginFrame(). The firmware calls update() several
  // times within a single frame (e.g. CrossPointWebServerActivity polls input
  // between handleClient() bursts, on top of the top-of-loop gpio.update() in
  // main.cpp). If edges were cleared on every update(), a key press drained by
  // an earlier update() would be wiped before a later update()'s wasPressed()
  // check could observe it — which made Back/Exit require repeated presses.
  // Latching edges for the whole frame keeps wasPressed() stable across all
  // update() calls in that frame, matching the on-device InputManager.

  // HalGPIO owns all SDL event polling so keyboard and quit events are never
  // split between two callers (SimulatorDisplay::presentIfNeeded only renders).
  SDL_Event e;
  while (SDL_PollEvent(&e) != 0) {
    if (e.type == SDL_QUIT) {
      SimulatorDisplay::requestQuit();
    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      if (e.key.keysym.scancode == HOME_KEY_SCANCODE) {
        inputMgr.beginHomeKey(SDL_GetTicks());
        continue;
      }
      if (e.key.keysym.scancode == SIMULATOR_SLEEP_SCANCODE) {
        requestSimulatorSleep();
        continue;
      }
      int btn = scancodeToButton(e.key.keysym.scancode);
      if (btn >= 0)
        inputMgr.pressButton(static_cast<uint8_t>(btn), SDL_GetTicks());
    } else if (e.type == SDL_KEYUP) {
      if (e.key.keysym.scancode == HOME_KEY_SCANCODE) {
        inputMgr.endHomeKey(SDL_GetTicks());
        continue;
      }
      int btn = scancodeToButton(e.key.keysym.scancode);
      if (btn >= 0)
        inputMgr.releaseButton(static_cast<uint8_t>(btn));
    } else if (e.type == SDL_MOUSEBUTTONDOWN &&
               e.button.button == SDL_BUTTON_LEFT) {
      mouseContactId = mouseChordContactId();
      mouseDown = true;
      const float logicalNx =
          static_cast<float>(e.button.x) /
          std::max(1, static_cast<int>(renderer.getScreenWidth()) - 1);
      const float logicalNy =
          static_cast<float>(e.button.y) /
          std::max(1, static_cast<int>(renderer.getScreenHeight()) - 1);
      beginTouch(logicalNx, logicalNy, mouseContactId);
    } else if (e.type == SDL_MOUSEMOTION && mouseDown) {
      const float logicalNx =
          static_cast<float>(e.motion.x) /
          std::max(1, static_cast<int>(renderer.getScreenWidth()) - 1);
      const float logicalNy =
          static_cast<float>(e.motion.y) /
          std::max(1, static_cast<int>(renderer.getScreenHeight()) - 1);
      moveTouch(logicalNx, logicalNy, mouseContactId);
    } else if (e.type == SDL_MOUSEBUTTONUP &&
               e.button.button == SDL_BUTTON_LEFT) {
      const float logicalNx =
          static_cast<float>(e.button.x) /
          std::max(1, static_cast<int>(renderer.getScreenWidth()) - 1);
      const float logicalNy =
          static_cast<float>(e.button.y) /
          std::max(1, static_cast<int>(renderer.getScreenHeight()) - 1);
      endTouch(logicalNx, logicalNy, mouseContactId);
      mouseDown = false;
    }
  }
  processSyntheticEvents();
  inputMgr.updateHolds(SDL_GetTicks());
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  const uint8_t *state = SDL_GetKeyboardState(nullptr);
  return inputMgr.isPressed(buttonIndex) || state[buttonScancode[buttonIndex]];
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  return inputMgr.wasPressed(buttonIndex);
}

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  return inputMgr.wasReleased(buttonIndex);
}

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed(); }

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased(); }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

unsigned long HalGPIO::getPowerButtonHeldTime() const {
  return inputMgr.getPowerButtonHeldTime();
}

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::hasHomeKey() const { return BoardConfig::hasHomeKey(); }

bool HalGPIO::wasHomeKeyPressed() const { return inputMgr.wasHomeKeyPressed(); }

bool HalGPIO::wasHomeKeyTapped() const { return inputMgr.wasHomeKeyTapped(); }

bool HalGPIO::wasHomeKeyLongPressed() const {
  return inputMgr.wasHomeKeyLongPressed();
}

bool HalGPIO::wasTouchTap(float &nx, float &ny) const {
  return inputMgr.wasTouchTap(nx, ny);
}

bool HalGPIO::wasTouchDown(float &nx, float &ny) const {
  return inputMgr.wasTouchPressedAt(nx, ny);
}

bool HalGPIO::wasTouchReleased() const { return inputMgr.wasTouchReleased(); }

bool HalGPIO::isTouchTapCandidate(float &nx, float &ny,
                                  unsigned long &heldMs) const {
  return inputMgr.isTouchTapCandidate(nx, ny, heldMs);
}

bool HalGPIO::isTouchHeldAt(float &nx, float &ny) const {
  return inputMgr.isTouchHeldAt(nx, ny);
}

bool HalGPIO::wasTouchLongPress(float &nx, float &ny) const {
  return inputMgr.wasTouchLongPress(nx, ny);
}

void HalGPIO::suppressTouchContact() { inputMgr.suppressTouchContact(); }

unsigned long HalGPIO::lastTouchHeldMs() const {
  return inputMgr.lastTouchHeldMs();
}

bool HalGPIO::wasSwipe(float &nxStart, float &nyStart, float &nxEnd,
                       float &nyEnd) const {
  return inputMgr.wasSwipe(nxStart, nyStart, nxEnd, nyEnd);
}

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }
void HalGPIO::setSharedConfirmPowerShortPressEmitsPower(bool enabled) {
  inputMgr.setSharedConfirmPowerShortPressEmitsPower(enabled);
}

bool HalGPIO::consumeSimulatorSleepRequest() {
  const bool requested = simulatorSleepRequested;
  simulatorSleepRequested = false;
  return requested;
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  if (SimulatorLifecycle::consumeWakeReason() ==
      SimulatorLifecycle::WakeReason::PowerButton) {
    return WakeupReason::PowerButton;
  }
  return WakeupReason::Other;
}
bool HalGPIO::isUsbConnected() const { return true; }
bool HalGPIO::wasUsbStateChanged() const { return false; }
void HalGPIO::startDeepSleep() {
  g_input = &inputMgr;
  inputMgr.clearAll();

  while (true) {
    processSyntheticEvents();
    if (SimulatorDisplay::shouldQuit())
      return;
    for (int button = 0; button < NUM_BUTTONS; button++) {
      if (inputMgr.isPressed(static_cast<uint8_t>(button))) {
        inputMgr.clearAll();
        SimulatorLifecycle::rebootAsPowerWake();
      }
    }

    SDL_Event e;
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        SimulatorDisplay::requestQuit();
        return;
      }

      if (e.type == SDL_KEYDOWN && !e.key.repeat &&
          scancodeToButton(e.key.keysym.scancode) >= 0) {
        inputMgr.clearAll();
        SimulatorLifecycle::rebootAsPowerWake();
      }
    }

    SDL_Delay(10);
  }
}
bool HalGPIO::verifyPowerButtonWakeup(uint16_t /*requiredDurationMs*/,
                                      bool /*shortPressAllowed*/) {
  return true;
}

HalGPIO gpio;
