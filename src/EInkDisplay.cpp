#include "EInkDisplay.h"

#include <cstring>

namespace {
constexpr uint8_t kGrayWhite = 255;
constexpr uint8_t kGrayLight = 200;
constexpr uint8_t kGrayDark = 96;
constexpr uint8_t kGrayBlack = 0;

uint32_t argbGray(uint8_t level) {
  return 0xFF000000u | (static_cast<uint32_t>(level) << 16) |
         (static_cast<uint32_t>(level) << 8) | level;
}
} // namespace

bool EInkDisplay::getBit(const uint8_t *buffer, int x, int y) {
  const int width = BoardConfig::ACTIVE.displayWidth;
  const int byteIdx = (y * width + x) / 8;
  const int bitIdx = 7 - (x % 8);
  return (buffer[byteIdx] & (1 << bitIdx)) != 0;
}

void EInkDisplay::clearGrayscalePlanes() {
  lsbPlane.fill(0);
  msbPlane.fill(0);
  lsbValid = false;
  msbValid = false;
}

void EInkDisplay::copyPlane(std::array<uint8_t, BUFFER_SIZE> &dst,
                            const uint8_t *src, bool &valid) {
  if (!src) {
    valid = false;
    dst.fill(0);
    return;
  }
  memcpy(dst.data(), src, BUFFER_SIZE);
  valid = true;
}

void EInkDisplay::clearScreen(uint8_t color) {
  memset(frameBuffer.data(), color, BUFFER_SIZE);
}

void EInkDisplay::drawImage(const uint8_t *imageData, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h, bool) {
  uint8_t *fb = getFrameBuffer();
  if (!fb)
    return;
  const uint16_t imageWidthBytes = w / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= getDisplayHeight())
      break;
    const uint16_t destOffset = destY * getDisplayWidthBytes() + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= getDisplayWidthBytes())
        break;
      fb[destOffset + col] = imageData[srcOffset + col];
    }
  }
}

void EInkDisplay::drawImageTransparent(const uint8_t *imageData, uint16_t x,
                                       uint16_t y, uint16_t w, uint16_t h,
                                       bool) {
  uint8_t *fb = getFrameBuffer();
  if (!fb)
    return;
  const uint16_t imageWidthBytes = w / 8;
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= getDisplayHeight())
      break;
    const uint16_t destOffset = destY * getDisplayWidthBytes() + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;
    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= getDisplayWidthBytes())
        break;
      fb[destOffset + col] &= imageData[srcOffset + col];
    }
  }
}

uint8_t *EInkDisplay::getFrameBuffer() {
  if (frameBufferLent)
    return nullptr;
  return frameBuffer.data();
}

const uint8_t *EInkDisplay::getFrameBuffer() const {
  if (frameBufferLent)
    return nullptr;
  return frameBuffer.data();
}

uint8_t *EInkDisplay::lendFrameBufferStorage(uint32_t *sizeOut) {
  if (sizeOut)
    *sizeOut = frameBufferLent ? 0 : BUFFER_SIZE;
  if (frameBufferLent)
    return nullptr;
  frameBufferLent = true;
  return frameBuffer.data();
}

void EInkDisplay::returnFrameBufferStorage() {
  if (!frameBufferLent)
    return;
  frameBuffer.fill(0xFF);
  frameBufferLent = false;
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t *lsbBuffer,
                                       const uint8_t *msbBuffer) {
  copyGrayscaleLsbBuffers(lsbBuffer);
  copyGrayscaleMsbBuffers(msbBuffer);
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer) {
  copyPlane(lsbPlane, lsbBuffer, lsbValid);
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t *msbBuffer) {
  copyPlane(msbPlane, msbBuffer, msbValid);
}

void EInkDisplay::cleanupGrayscaleBuffers(const uint8_t *bwBuffer) {
  if (bwBuffer) {
    memcpy(bwBase.data(), bwBuffer, BUFFER_SIZE);
    bwBaseValid = true;
    clearGrayscalePlanes();
  } else {
    bwBaseValid = false;
    bwBase.fill(0);
    clearGrayscalePlanes();
  }
}

void EInkDisplay::writeGrayscalePlaneStrip(GrayPlane plane, const uint8_t *rows,
                                           uint16_t yStart, uint16_t numRows) {
  if (!rows || numRows == 0 || yStart >= getDisplayHeight())
    return;

  const uint16_t rowsToCopy = (yStart + numRows > getDisplayHeight())
                                  ? (getDisplayHeight() - yStart)
                                  : numRows;
  const size_t offset = static_cast<size_t>(yStart) * getDisplayWidthBytes();
  const size_t byteCount =
      static_cast<size_t>(rowsToCopy) * getDisplayWidthBytes();
  auto &dst = (plane == GRAY_PLANE_LSB) ? lsbPlane : msbPlane;
  memcpy(dst.data() + offset, rows, byteCount);
  if (plane == GRAY_PLANE_LSB)
    lsbValid = true;
  else
    msbValid = true;
}

void EInkDisplay::snapshotBwBase() {
  memcpy(bwBase.data(), frameBuffer.data(), BUFFER_SIZE);
  bwBaseValid = true;
  clearGrayscalePlanes();
}

void EInkDisplay::composeBwArgb(uint32_t *dst, bool inverted) const {
  const uint8_t *fb = getFrameBuffer();
  if (!fb)
    return;
  const int width = getDisplayWidth();
  const int height = getDisplayHeight();
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const bool white = getBit(fb, x, y);
      dst[y * width + x] = (white != inverted) ? 0xFFFFFFFFu : 0xFF000000u;
    }
  }
}

void EInkDisplay::composeGrayscaleArgb(uint32_t *dst, bool inverted) const {
  const uint8_t *base = bwBaseValid ? bwBase.data() : getFrameBuffer();
  if (!base)
    return;
  const int width = getDisplayWidth();
  const int height = getDisplayHeight();
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const bool baseWhite = getBit(base, x, y);
      const bool lsbActive = lsbValid && getBit(lsbPlane.data(), x, y);
      const bool msbActive = msbValid && getBit(msbPlane.data(), x, y);

      uint8_t level = kGrayWhite;
      if (!baseWhite) {
        if (msbActive)
          level = lsbActive ? kGrayDark : kGrayLight;
        else if (lsbActive)
          level = kGrayDark;
        else
          level = kGrayBlack;
      }

      if (inverted)
        level = static_cast<uint8_t>(255 - level);
      dst[y * width + x] = argbGray(level);
    }
  }
}
