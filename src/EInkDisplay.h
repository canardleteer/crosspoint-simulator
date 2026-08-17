#pragma once
#include <BoardConfig.h>
#include <array>
#include <cstdint>
#ifndef EPD_SCLK
#define EPD_SCLK 0
#endif
#ifndef EPD_MOSI
#define EPD_MOSI 0
#endif
#ifndef EPD_CS
#define EPD_CS 0
#endif
#ifndef EPD_DC
#define EPD_DC 0
#endif
#ifndef EPD_RST
#define EPD_RST 0
#endif
#ifndef EPD_BUSY
#define EPD_BUSY 0
#endif

class EInkDisplay {
public:
  // Compile-time maxima for the devices in this binary. Live geometry is
  // BoardConfig::ACTIVE; a dual X3+X4 build allocates the larger panel.
  static constexpr uint16_t DISPLAY_WIDTH = BoardConfig::MAX_DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = BoardConfig::MAX_DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = BoardConfig::MAX_FRAMEBUFFER_BYTES;

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum GrayPlane { GRAY_PLANE_LSB, GRAY_PLANE_MSB };

  EInkDisplay() = default;
  EInkDisplay(int, int, int, int, int, int) {}
  void begin();
  void setDisplayX3() {}
  void skipInitialResync() {}
  void requestResync(uint8_t = 0) {}
  void clearScreen(uint8_t color) const;
  void drawImage(const uint8_t *imageData, uint16_t x, uint16_t y, uint16_t w,
                 uint16_t h, bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t *imageData, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h, bool fromProgmem = false) const;
  void setInverted(bool value) { inverted = value; }
  bool toggleInverted() {
    inverted = !inverted;
    return inverted;
  }
  bool isInverted() const { return inverted; }
  void displayBuffer(RefreshMode mode, bool turnOffScreen);
  void displayBufferAsync(RefreshMode mode);
  void displayBufferAsyncNoShadow(RefreshMode mode);
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const { return false; }
  void refreshDisplay(RefreshMode mode, bool turnOffScreen);
  void setBusyWaitSliceHook(bool (*)(int8_t, uint8_t)) {}
  void deepSleep();

  uint8_t *getFrameBuffer();
  uint8_t *getFrameBuffer() const;
  uint8_t *lendFrameBufferStorage(uint32_t *sizeOut);
  void returnFrameBufferStorage();
  uint8_t *lendBuildStorage(uint32_t *sizeOut) {
    return lendFrameBufferStorage(sizeOut);
  }
  void returnBuildStorage() { returnFrameBufferStorage(); }
  bool isFrameBufferLent() const { return frameBufferLent; }

  uint16_t getDisplayWidth() const { return BoardConfig::ACTIVE.displayWidth; }
  uint16_t getDisplayHeight() const { return BoardConfig::ACTIVE.displayHeight; }
  uint16_t getDisplayWidthBytes() const {
    return BoardConfig::ACTIVE.displayWidth / 8;
  }
  uint32_t getBufferSize() const {
    return static_cast<uint32_t>(getDisplayWidthBytes()) * getDisplayHeight();
  }

  void copyGrayscaleBuffers(const uint8_t *lsbBuffer, const uint8_t *msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t *lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t *msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t *bwBuffer);
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH,
                            bool turnOffScreen = false);
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {}
  void displayGrayBuffer(bool turnOffScreen = false,
                         const unsigned char *lut = nullptr,
                         bool factoryMode = false);
  void writeGrayscalePlaneStrip(GrayPlane plane, const uint8_t *rows,
                                uint16_t yStart, uint16_t numRows);
  bool supportsStripGrayscale() const { return true; }
  bool combinesGrayscaleBase() const { return BoardConfig::isPaperMono(); }
  void snapshotBwBase();
  void composeBwArgb(uint32_t *dst, bool inverted) const;
  void composeGrayscaleArgb(uint32_t *dst, bool inverted) const;

private:
  mutable std::array<uint8_t, BUFFER_SIZE> frameBuffer{};
  std::array<uint8_t, BUFFER_SIZE> bwBase{};
  std::array<uint8_t, BUFFER_SIZE> lsbPlane{};
  std::array<uint8_t, BUFFER_SIZE> msbPlane{};
  bool bwBaseValid = false;
  bool lsbValid = false;
  bool msbValid = false;
  bool frameBufferLent = false;
  bool inverted = false;

  static bool getBit(const uint8_t *buffer, int x, int y);
  void copyPlane(std::array<uint8_t, BUFFER_SIZE> &dst, const uint8_t *src,
                 bool &valid);
  void clearGrayscalePlanes();
};

// Stub LUTs - unused in simulator but must exist so GfxRenderer.cpp compiles.
inline const unsigned char lut_factory_fast[] = {0};
inline const unsigned char lut_factory_quality[] = {0};
