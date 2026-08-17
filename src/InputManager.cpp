#include "InputManager.h"

#include <cmath>

InputManager::ButtonHook InputManager::s_buttonHook = nullptr;
bool InputManager::s_sharedConfirmPowerShortPressEmitsPower = false;

uint16_t InputManager::panelX(float nx) {
  const uint16_t width = BoardConfig::ACTIVE.displayWidth;
  if (width <= 1)
    return 0;
  if (nx <= 0.0f)
    return 0;
  if (nx >= 1.0f)
    return static_cast<uint16_t>(width - 1);
  return static_cast<uint16_t>(nx * static_cast<float>(width - 1));
}

uint16_t InputManager::panelY(float ny) {
  const uint16_t height = BoardConfig::ACTIVE.displayHeight;
  if (height <= 1)
    return 0;
  if (ny <= 0.0f)
    return 0;
  if (ny >= 1.0f)
    return static_cast<uint16_t>(height - 1);
  return static_cast<uint16_t>(ny * static_cast<float>(height - 1));
}

float InputManager::panelDx(float nx0, float nx1) {
  return (nx1 - nx0) * static_cast<float>(BoardConfig::ACTIVE.displayWidth);
}

float InputManager::panelDy(float ny0, float ny1) {
  return (ny1 - ny0) * static_cast<float>(BoardConfig::ACTIVE.displayHeight);
}

InputManager::Contact *InputManager::contactById(uint8_t id) {
  if (id >= MAX_TOUCH_CONTACTS)
    return nullptr;
  return &contacts[id];
}

const InputManager::Contact *InputManager::contactById(uint8_t id) const {
  if (id >= MAX_TOUCH_CONTACTS)
    return nullptr;
  return &contacts[id];
}

void InputManager::beginFrame() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pressedThisFrame[i] = false;
    releasedThisFrame[i] = false;
  }
  for (auto &contact : contacts) {
    contact.pressedThisFrame = false;
    contact.releasedThisFrame = false;
    contact.longPressThisFrame = false;
  }
  activityThisFrame = false;
  multiTouchSwipeThisFrame = false;
  homeKeyPressedThisFrame = false;
  homeKeyTappedThisFrame = false;
  homeKeyLongPressedThisFrame = false;
}

void InputManager::clearAll() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    buttonDown[i] = false;
    pressedThisFrame[i] = false;
    releasedThisFrame[i] = false;
    buttonPressTime[i] = 0;
  }
  for (auto &contact : contacts)
    contact = {};
  activityThisFrame = false;
  multiTouchSwipeThisFrame = false;
  multiTouchTracking = false;
  homeKeyDown = false;
  homeKeyPressedThisFrame = false;
  homeKeyTappedThisFrame = false;
  homeKeyLongPressedThisFrame = false;
  homeKeyLongFired = false;
  homeKeyPressedAt = 0;
}

uint8_t InputManager::getState() const {
  uint8_t state = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (buttonDown[i])
      state = static_cast<uint8_t>(state | (1u << i));
  }
  return state;
}

void InputManager::pressButton(uint8_t buttonIndex, unsigned long nowTicks) {
  if (buttonIndex >= NUM_BUTTONS)
    return;
  lastNowTicks = nowTicks;
  buttonDown[buttonIndex] = true;
  pressedThisFrame[buttonIndex] = true;
  buttonPressTime[buttonIndex] = nowTicks;
}

void InputManager::releaseButton(uint8_t buttonIndex) {
  if (buttonIndex >= NUM_BUTTONS)
    return;
  buttonDown[buttonIndex] = false;
  releasedThisFrame[buttonIndex] = true;
}

void InputManager::setButtonDown(uint8_t buttonIndex, bool down,
                                 unsigned long nowTicks) {
  if (buttonIndex >= NUM_BUTTONS)
    return;
  if (down)
    pressButton(buttonIndex, nowTicks);
  else
    releaseButton(buttonIndex);
}

bool InputManager::isPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  return buttonDown[buttonIndex];
}

bool InputManager::wasPressed(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  return pressedThisFrame[buttonIndex];
}

bool InputManager::wasReleased(uint8_t buttonIndex) const {
  if (buttonIndex >= NUM_BUTTONS)
    return false;
  return releasedThisFrame[buttonIndex];
}

bool InputManager::wasAnyPressed() const {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (pressedThisFrame[i])
      return true;
  }
  return false;
}

bool InputManager::wasAnyReleased() const {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (releasedThisFrame[i])
      return true;
  }
  return false;
}

unsigned long InputManager::getHeldTime() const {
  unsigned long maxHeld = 0;
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (buttonDown[i] && buttonPressTime[i] > 0) {
      const unsigned long held = lastNowTicks - buttonPressTime[i];
      if (held > maxHeld)
        maxHeld = held;
    }
  }
  return maxHeld;
}

unsigned long InputManager::getPowerButtonHeldTime() const {
  if (!buttonDown[BTN_POWER] || buttonPressTime[BTN_POWER] == 0)
    return 0;
  return lastNowTicks - buttonPressTime[BTN_POWER];
}

const char *InputManager::getButtonName(uint8_t buttonIndex) {
  switch (buttonIndex) {
  case BTN_BACK:
    return "Back";
  case BTN_CONFIRM:
    return "Confirm";
  case BTN_LEFT:
    return "Left";
  case BTN_RIGHT:
    return "Right";
  case BTN_UP:
    return "Up";
  case BTN_DOWN:
    return "Down";
  case BTN_POWER:
    return "Power";
  default:
    return "Unknown";
  }
}

void InputManager::updateContactMovement(Contact &contact, float nx, float ny) {
  contact.currentNx = nx;
  contact.currentNy = ny;
  const float dx = panelDx(contact.startNx, nx);
  const float dy = panelDy(contact.startNy, ny);
  if (std::abs(dx) > static_cast<float>(TOUCH_TAP_SLOP_PX) ||
      std::abs(dy) > static_cast<float>(TOUCH_TAP_SLOP_PX)) {
    contact.movedBeyondTapSlop = true;
  }
}

void InputManager::beginTouchContact(uint8_t id, float nx, float ny,
                                     unsigned long nowTicks) {
  if (!hasTouch())
    return;
  Contact *contact = contactById(id);
  if (!contact)
    return;
  *contact = {};
  contact->down = true;
  contact->pressedThisFrame = true;
  contact->startNx = nx;
  contact->startNy = ny;
  contact->currentNx = nx;
  contact->currentNy = ny;
  contact->pressedAt = nowTicks;
  activityThisFrame = true;

  uint8_t downCount = 0;
  float sumNx = 0.0f;
  float sumNy = 0.0f;
  for (const auto &item : contacts) {
    if (!item.down)
      continue;
    downCount++;
    sumNx += item.startNx;
    sumNy += item.startNy;
  }
  if (supportsMultiTouch() && downCount >= 2 && !multiTouchTracking) {
    multiTouchTracking = true;
    multiTouchStartedAt = nowTicks;
    multiTouchStartNx = sumNx / static_cast<float>(downCount);
    multiTouchStartNy = sumNy / static_cast<float>(downCount);
    multiTouchSwipeCount = downCount;
  }
}

void InputManager::moveTouchContact(uint8_t id, float nx, float ny) {
  Contact *contact = contactById(id);
  if (!contact || !contact->down)
    return;
  updateContactMovement(*contact, nx, ny);
}

void InputManager::endTouchContact(uint8_t id, float nx, float ny,
                                   unsigned long nowTicks) {
  Contact *contact = contactById(id);
  if (!contact || !contact->down)
    return;
  updateContactMovement(*contact, nx, ny);
  contact->down = false;
  contact->releasedThisFrame = true;
  contact->lastHeldMs = nowTicks - contact->pressedAt;
  activityThisFrame = true;
  refreshMultiTouchSwipe(nowTicks);
}

void InputManager::refreshMultiTouchSwipe(unsigned long nowTicks) {
  if (!multiTouchTracking)
    return;

  uint8_t downCount = 0;
  uint8_t releasedCount = 0;
  float sumEndNx = 0.0f;
  float sumEndNy = 0.0f;
  for (const auto &item : contacts) {
    if (item.down) {
      downCount++;
      continue;
    }
    if (item.releasedThisFrame || item.lastHeldMs > 0) {
      releasedCount++;
      sumEndNx += item.currentNx;
      sumEndNy += item.currentNy;
    }
  }
  if (downCount > 0)
    return;

  multiTouchTracking = false;
  if (!supportsMultiTouch() || releasedCount < 2 ||
      releasedCount > MAX_TOUCH_CONTACTS)
    return;

  const unsigned long duration = nowTicks - multiTouchStartedAt;
  if (duration > TOUCH_MULTI_SWIPE_MAX_MS)
    return;

  multiTouchEndNx = sumEndNx / static_cast<float>(releasedCount);
  multiTouchEndNy = sumEndNy / static_cast<float>(releasedCount);
  const float dx = panelDx(multiTouchStartNx, multiTouchEndNx);
  const float dy = panelDy(multiTouchStartNy, multiTouchEndNy);
  if (std::abs(dx) < static_cast<float>(TOUCH_SWIPE_MIN_PX) &&
      std::abs(dy) < static_cast<float>(TOUCH_SWIPE_MIN_PX))
    return;

  multiTouchSwipeThisFrame = true;
  multiTouchSwipeCount = releasedCount;
  multiTouchDurationMs = duration;
}

void InputManager::beginHomeKey(unsigned long nowTicks) {
  if (!BoardConfig::hasHomeKey() || homeKeyDown)
    return;
  homeKeyDown = true;
  homeKeyPressedThisFrame = true;
  homeKeyLongFired = false;
  homeKeyPressedAt = nowTicks;
}

void InputManager::endHomeKey(unsigned long nowTicks) {
  if (!homeKeyDown)
    return;
  if (!homeKeyLongFired && nowTicks - homeKeyPressedAt < HOME_KEY_LONG_PRESS_MS)
    homeKeyTappedThisFrame = true;
  homeKeyDown = false;
}

void InputManager::updateHolds(unsigned long nowTicks) {
  lastNowTicks = nowTicks;
  for (auto &contact : contacts) {
    if (contact.down && !contact.movedBeyondTapSlop && !contact.longPressFired &&
        !contact.suppressed &&
        nowTicks - contact.pressedAt >= TOUCH_LONG_PRESS_MS) {
      contact.longPressFired = true;
      contact.longPressThisFrame = true;
    }
  }
  if (homeKeyDown && !homeKeyLongFired &&
      nowTicks - homeKeyPressedAt >= HOME_KEY_LONG_PRESS_MS) {
    homeKeyLongFired = true;
    homeKeyLongPressedThisFrame = true;
  }
}

InputManager::TouchSnapshot InputManager::getTouchSnapshot() const {
  TouchSnapshot snapshot{};
  snapshot.idsStable = true;
  for (uint8_t i = 0; i < MAX_TOUCH_CONTACTS; i++) {
    if (!contacts[i].down)
      continue;
    if (snapshot.count < MAX_TOUCH_CONTACTS) {
      snapshot.points[snapshot.count].id = i;
      snapshot.points[snapshot.count].point = {true, panelX(contacts[i].currentNx),
                                               panelY(contacts[i].currentNy),
                                               contacts[i].pressedAt};
      snapshot.count++;
    }
    snapshot.reportedCount++;
  }
  return snapshot;
}

InputManager::TouchPoint InputManager::getTouchPoint() const {
  if (!contacts[0].down)
    return {};
  return {true, panelX(contacts[0].currentNx), panelY(contacts[0].currentNy),
          contacts[0].pressedAt};
}

bool InputManager::isTouchPressed() const { return contacts[0].down; }

bool InputManager::wasTouchPressed() const {
  return contacts[0].pressedThisFrame;
}

bool InputManager::wasTouchReleased() const {
  return contacts[0].releasedThisFrame;
}

bool InputManager::wasTouchTap(float &nx, float &ny) const {
  const Contact &contact = contacts[0];
  if (!contact.releasedThisFrame || contact.movedBeyondTapSlop ||
      contact.suppressed)
    return false;
  nx = contact.startNx;
  ny = contact.startNy;
  return true;
}

bool InputManager::wasTouchPressedAt(float &nx, float &ny) const {
  if (!contacts[0].pressedThisFrame)
    return false;
  nx = contacts[0].startNx;
  ny = contacts[0].startNy;
  return true;
}

bool InputManager::isTouchTapCandidate(float &nx, float &ny,
                                       unsigned long &heldMs) const {
  const Contact &contact = contacts[0];
  if (!contact.down || contact.movedBeyondTapSlop || contact.suppressed) {
    heldMs = 0;
    return false;
  }
  nx = contact.startNx;
  ny = contact.startNy;
  heldMs = lastNowTicks - contact.pressedAt;
  return true;
}

bool InputManager::isTouchHeldAt(float &nx, float &ny) const {
  const Contact &contact = contacts[0];
  if (!contact.down || contact.suppressed)
    return false;
  nx = contact.currentNx;
  ny = contact.currentNy;
  return true;
}

unsigned long InputManager::lastTouchHeldMs() const {
  return contacts[0].lastHeldMs;
}

bool InputManager::wasSwipe(float &nxStart, float &nyStart, float &nxEnd,
                            float &nyEnd) const {
  const Contact &contact = contacts[0];
  if (!contact.releasedThisFrame || contact.suppressed ||
      contact.lastHeldMs > TOUCH_SWIPE_MAX_MS)
    return false;
  const float dx = panelDx(contact.startNx, contact.currentNx);
  const float dy = panelDy(contact.startNy, contact.currentNy);
  if (std::abs(dx) < static_cast<float>(TOUCH_SWIPE_MIN_PX) &&
      std::abs(dy) < static_cast<float>(TOUCH_SWIPE_MIN_PX))
    return false;
  nxStart = contact.startNx;
  nyStart = contact.startNy;
  nxEnd = contact.currentNx;
  nyEnd = contact.currentNy;
  return true;
}

bool InputManager::wasMultiTouchSwipe(uint8_t &contactCount, float &nxStart,
                                      float &nyStart, float &nxEnd, float &nyEnd,
                                      unsigned long &durationMs) const {
  if (!multiTouchSwipeThisFrame)
    return false;
  contactCount = multiTouchSwipeCount;
  nxStart = multiTouchStartNx;
  nyStart = multiTouchStartNy;
  nxEnd = multiTouchEndNx;
  nyEnd = multiTouchEndNy;
  durationMs = multiTouchDurationMs;
  return true;
}

bool InputManager::wasTouchLongPress(float &nx, float &ny) const {
  const Contact &contact = contacts[0];
  if (!contact.longPressThisFrame || contact.suppressed)
    return false;
  nx = contact.startNx;
  ny = contact.startNy;
  return true;
}

void InputManager::suppressTouchContact() {
  for (auto &contact : contacts) {
    if (contact.down || contact.releasedThisFrame)
      contact.suppressed = true;
  }
}

bool InputManager::wasTouchActivity() const { return activityThisFrame; }
bool InputManager::wasHomeKeyPressed() const { return homeKeyPressedThisFrame; }
bool InputManager::wasHomeKeyTapped() const { return homeKeyTappedThisFrame; }
bool InputManager::wasHomeKeyLongPressed() const {
  return homeKeyLongPressedThisFrame;
}
