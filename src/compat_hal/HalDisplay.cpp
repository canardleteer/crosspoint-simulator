#include "HalDisplay.h"

HalDisplay::HalDisplay() {}
HalDisplay::~HalDisplay() {}

void HalDisplay::begin() { einkDisplay.begin(); }

void HalDisplay::begin(bool /*seamless*/) { begin(); }

void HalDisplay::clearScreen(uint8_t color) const {
  einkDisplay.clearScreen(color);
}

void HalDisplay::drawImage(const uint8_t *imageData, uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t *imageData, uint16_t x,
                                      uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::setInverted(bool value) { einkDisplay.setInverted(value); }

bool HalDisplay::toggleInverted() { return einkDisplay.toggleInverted(); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  einkDisplay.displayBuffer(static_cast<EInkDisplay::RefreshMode>(mode),
                            turnOffScreen);
}

void HalDisplay::displayBufferAsync(RefreshMode mode) {
  einkDisplay.displayBufferAsync(static_cast<EInkDisplay::RefreshMode>(mode));
}

void HalDisplay::waitRefreshComplete() { einkDisplay.waitRefreshComplete(); }

bool HalDisplay::supportsAsyncRefresh() const {
  return einkDisplay.supportsAsyncRefresh();
}

void HalDisplay::displayWindow(int, int, int, int) {
  refreshDisplay(RefreshMode::FAST_REFRESH, false);
}

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) {
  einkDisplay.refreshDisplay(static_cast<EInkDisplay::RefreshMode>(mode),
                             turnOffScreen);
}

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t *HalDisplay::getFrameBuffer() const {
  return einkDisplay.getFrameBuffer();
}

uint8_t *HalDisplay::lendFrameBufferStorage(uint32_t *sizeOut) {
  return einkDisplay.lendFrameBufferStorage(sizeOut);
}

void HalDisplay::returnFrameBufferStorage() {
  einkDisplay.returnFrameBufferStorage();
}

void HalDisplay::copyGrayscaleBuffers(const uint8_t *lsbBuffer,
                                      const uint8_t *msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen) {
  einkDisplay.displayGrayscaleBase(
      static_cast<EInkDisplay::RefreshMode>(fallback), turnOffScreen);
}

void HalDisplay::preconditionGrayscale() { einkDisplay.preconditionGrayscale(); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w,
                                       uint16_t h) {
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer) {
  einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t *msbBuffer) {
  einkDisplay.copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {
  einkDisplay.cleanupGrayscaleBuffers(bwBuffer);
}

void HalDisplay::displayGrayBuffer(bool turnOffScreen, const unsigned char *lut,
                                   bool factoryMode) {
  einkDisplay.displayGrayBuffer(turnOffScreen, lut, factoryMode);
}

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t *rows,
                                          uint16_t yStart, uint16_t numRows) {
  einkDisplay.writeGrayscalePlaneStrip(lsbPlane ? EInkDisplay::GRAY_PLANE_LSB
                                                : EInkDisplay::GRAY_PLANE_MSB,
                                       rows, yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const {
  return einkDisplay.supportsStripGrayscale();
}

bool HalDisplay::combinesGrayscaleBase() const {
  return einkDisplay.combinesGrayscaleBase();
}

uint16_t HalDisplay::getDisplayWidth() const {
  return einkDisplay.getDisplayWidth();
}

uint16_t HalDisplay::getDisplayHeight() const {
  return einkDisplay.getDisplayHeight();
}

uint16_t HalDisplay::getDisplayWidthBytes() const {
  return einkDisplay.getDisplayWidthBytes();
}

uint32_t HalDisplay::getBufferSize() const {
  return einkDisplay.getBufferSize();
}

HalDisplay display;
