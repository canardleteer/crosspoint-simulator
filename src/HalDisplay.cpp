#include "HalDisplay.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <SDL.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static SDL_Window *window = nullptr;
static SDL_Renderer *sdl_renderer = nullptr;
static SDL_Texture *texture = nullptr;
// Render the simulator at full panel size. The previous 0.5x window was too
// small. With 1:1 pixel mapping, the simulator can be used for testing fine
// details.
static constexpr int SIMULATOR_WINDOW_SCALE = 1;

// Pixel buffer written by the render task, read by the main thread for
// SDL_RenderPresent. On macOS, SDL calls must happen on the main thread.
static uint32_t
    pixelBuf[BoardConfig::MAX_DISPLAY_WIDTH * BoardConfig::MAX_DISPLAY_HEIGHT];
static std::mutex pixelBufMutex;
static std::atomic<bool> pendingPresent{false};
// Written by HalGPIO::update() (which owns SDL event polling); read by
// shouldQuit().
std::atomic<bool> quitRequested{false};

static int currentWindowWidth = 0;
static int currentWindowHeight = 0;

namespace {

struct ScreenshotEvent {
  unsigned long atMs;
  std::string path;
  bool handled = false;
};

std::vector<ScreenshotEvent> screenshotEvents;
bool screenshotEventsInitialized = false;
const std::thread::id simulatorMainThread = std::this_thread::get_id();

void initializeScreenshotEvents() {
  if (screenshotEventsInitialized)
    return;
  screenshotEventsInitialized = true;

  const char *schedule = std::getenv("CROSSPOINT_SIM_SCREENSHOTS");
  if (!schedule || schedule[0] == '\0')
    return;

  const std::string spec(schedule);
  size_t start = 0;
  while (start < spec.size()) {
    const size_t end = spec.find(';', start);
    const std::string item = spec.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    const size_t colon = item.find(':');
    if (colon != std::string::npos && colon + 1 < item.size()) {
      screenshotEvents.push_back(
          {std::strtoul(item.substr(0, colon).c_str(), nullptr, 10),
           item.substr(colon + 1)});
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
}

bool hasDueScreenshot() {
  initializeScreenshotEvents();
  const unsigned long now = millis();
  for (const auto &event : screenshotEvents) {
    if (!event.handled && event.atMs <= now)
      return true;
  }
  return false;
}

bool saveRendererBmp(const std::string &path) {
  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(sdl_renderer, &width, &height) != 0 ||
      width <= 0 || height <= 0) {
    std::cerr << "[SIM] Cannot determine screenshot size: " << SDL_GetError()
              << std::endl;
    return false;
  }

  std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
  if (SDL_RenderReadPixels(sdl_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                           pixels.data(), width * sizeof(uint32_t)) != 0) {
    std::cerr << "[SIM] Cannot read screenshot pixels: " << SDL_GetError()
              << std::endl;
    return false;
  }

  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
      pixels.data(), width, height, 32, width * sizeof(uint32_t),
      SDL_PIXELFORMAT_ARGB8888);
  if (!surface) {
    std::cerr << "[SIM] Cannot create screenshot surface: " << SDL_GetError()
              << std::endl;
    return false;
  }

  const bool saved = SDL_SaveBMP(surface, path.c_str()) == 0;
  if (!saved) {
    std::cerr << "[SIM] Cannot save screenshot " << path << ": "
              << SDL_GetError() << std::endl;
  } else {
    std::cerr << "[SIM] Saved screenshot: " << path << std::endl;
  }
  SDL_FreeSurface(surface);
  return saved;
}

void captureDueScreenshots() {
  const unsigned long now = millis();
  for (auto &event : screenshotEvents) {
    if (event.handled || event.atMs > now)
      continue;
    event.handled = true;
    saveRendererBmp(event.path);
  }
}

} // namespace

static bool isPortraitOrientation(GfxRenderer::Orientation orientation) {
  return orientation == GfxRenderer::Portrait ||
         orientation == GfxRenderer::PortraitInverted;
}

static void getLogicalWindowSize(GfxRenderer::Orientation orientation,
                                 int *width, int *height) {
  const bool isPortrait = isPortraitOrientation(orientation);
  const int panelW = BoardConfig::ACTIVE.displayWidth;
  const int panelH = BoardConfig::ACTIVE.displayHeight;
  *width = (isPortrait ? panelH : panelW) * SIMULATOR_WINDOW_SCALE;
  *height = (isPortrait ? panelW : panelH) * SIMULATOR_WINDOW_SCALE;
}

static void applyWindowGeometryIfNeeded(GfxRenderer::Orientation orientation) {
  if (!window || !sdl_renderer)
    return;

  int winW = 0;
  int winH = 0;
  getLogicalWindowSize(orientation, &winW, &winH);
  if (winW == currentWindowWidth && winH == currentWindowHeight)
    return;

  SDL_SetWindowSize(window, winW, winH);
  SDL_RenderSetLogicalSize(sdl_renderer, winW, winH);
  currentWindowWidth = winW;
  currentWindowHeight = winH;
}

HalDisplay::HalDisplay() {}
HalDisplay::~HalDisplay() {}

static const char *windowTitle() {
  using Board = BoardConfig::Board;
  using DisplayController = BoardConfig::DisplayController;
  const auto &profile = BoardConfig::ACTIVE;
  switch (profile.board) {
  case Board::Sticky:
    return "Simulator - Seeed Sticky (SSD1677)";
  case Board::PaperMono:
    return "Simulator - M5Stack Paper Mono (SSD1677)";
  case Board::XteinkX4Pro:
    if (profile.displayController == DisplayController::UC8179)
      return "Simulator - XTEINK X4 Pro (UC8179)";
    if (profile.displayController == DisplayController::UC8279)
      return "Simulator - XTEINK X4 Pro (UC8279)";
    return "Simulator - XTEINK X4 Pro (SSD1677)";
  case Board::XteinkX3Uc8279:
    return "Simulator - XTEINK X3 (UC8279d)";
  case Board::XteinkX3:
    return "Simulator - XTEINK X3 (UC8253)";
  case Board::XteinkX4:
  default:
    if (profile.displayController == DisplayController::UC8179)
      return "Simulator - XTEINK X4 (UC8179)";
    if (profile.displayController == DisplayController::UC8279)
      return "Simulator - XTEINK X4 (UC8279)";
    return "Simulator - XTEINK X4 (SSD1677)";
  }
}

void HalDisplay::begin() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError()
              << std::endl;
    return;
  }

  int winW = 0;
  int winH = 0;
  extern GfxRenderer renderer;
  getLogicalWindowSize(renderer.getOrientation(), &winW, &winH);

  // SDL_WINDOW_ALLOW_HIGHDPI lets the renderer use full Retina/HiDPI pixels on
  // macOS so we get crisp 1:1 rendering instead of a blurry upscale.
  window = SDL_CreateWindow(windowTitle(), SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, winW, winH,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  // Keep all rendering logic in logical (winW×winH) coordinates; SDL maps to
  // drawable pixels.
  SDL_RenderSetLogicalSize(sdl_renderer, winW, winH);
  currentWindowWidth = winW;
  currentWindowHeight = winH;

  // Linear filtering: Bayer-dithered pixels average to correct gray at scaled
  // sizes rather than showing harsh black/white patterns.
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, getDisplayWidth(),
                              getDisplayHeight());
}

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

void HalDisplay::setInverted(bool value) { inverted = value; }

bool HalDisplay::toggleInverted() {
  inverted = !inverted;
  return inverted;
}

bool HalDisplay::isInverted() const { return inverted; }

void HalDisplay::displayBuffer(RefreshMode mode, bool turnOffScreen) {
  refreshDisplay(mode, turnOffScreen);
  if (std::this_thread::get_id() == simulatorMainThread) {
    presentIfNeeded();
  }
}

void HalDisplay::displayBufferAsync(RefreshMode mode) {
  // SDL presentation is already handed off to the main thread. The framebuffer
  // conversion itself remains synchronous, so advertise no genuine overlap.
  refreshDisplay(mode, false);
}

void HalDisplay::waitRefreshComplete() {}

bool HalDisplay::supportsAsyncRefresh() const { return false; }

void HalDisplay::displayWindow(int, int, int, int) {
  refreshDisplay(RefreshMode::FAST_REFRESH, false);
}

// Called from the render task (background thread): convert framebuffer to
// pixels and flag for present.
void HalDisplay::refreshDisplay(RefreshMode /*mode*/, bool /*turnOffScreen*/) {
  einkDisplay.snapshotBwBase();
  const std::lock_guard<std::mutex> lock(pixelBufMutex);
  einkDisplay.composeBwArgb(pixelBuf, inverted);
  pendingPresent.store(true);
}

// Called from the main thread (simulator_main.cpp) to push pixels to SDL.
void HalDisplay::presentIfNeeded() {
  const bool screenshotDue = hasDueScreenshot();
  if (!pendingPresent.exchange(false) && !screenshotDue)
    return;

  if (!texture || !sdl_renderer)
    return;

  extern GfxRenderer renderer;
  const GfxRenderer::Orientation orientation = renderer.getOrientation();
  applyWindowGeometryIfNeeded(orientation);

  {
    const std::lock_guard<std::mutex> lock(pixelBufMutex);
    SDL_UpdateTexture(texture, nullptr, pixelBuf,
                      getDisplayWidth() * sizeof(uint32_t));
  }
  SDL_RenderClear(sdl_renderer);

  // For portrait modes the landscape panel texture must be rotated to fill the
  // portrait window. SDL_RenderCopyEx rotates around the centre of dst, so dst
  // must stay landscape-oriented and be offset so its centre coincides with the
  // window centre. After rotation the result fills the portrait window.
  //
  // Portrait rotateCoordinates stores content rotated 90° CCW in the physical
  // buffer, so we rotate +90° CW here to undo it. PortraitInverted stores
  // content rotated 90° CW → undo with -90°.
  switch (orientation) {
  case GfxRenderer::Portrait: {
    // dst centre = window centre, landscape-sized panel texture.
    const int panelW = getDisplayWidth();
    const int panelH = getDisplayHeight();
    SDL_Rect dst = {(panelH - panelW) / 2, panelW / 2 - panelH / 2, panelW,
                    panelH};
    SDL_RenderCopyEx(sdl_renderer, texture, nullptr, &dst, 90.0, nullptr,
                     SDL_FLIP_NONE);
    break;
  }
  case GfxRenderer::PortraitInverted: {
    const int panelW = getDisplayWidth();
    const int panelH = getDisplayHeight();
    SDL_Rect dst = {(panelH - panelW) / 2, panelW / 2 - panelH / 2, panelW,
                    panelH};
    SDL_RenderCopyEx(sdl_renderer, texture, nullptr, &dst, -90.0, nullptr,
                     SDL_FLIP_NONE);
    break;
  }
  case GfxRenderer::LandscapeClockwise: {
    SDL_Rect dst = {0, 0, getDisplayWidth(), getDisplayHeight()};
    SDL_RenderCopyEx(sdl_renderer, texture, nullptr, &dst, 180.0, nullptr,
                     SDL_FLIP_NONE);
    break;
  }
  default: {
    SDL_Rect dst = {0, 0, getDisplayWidth(), getDisplayHeight()};
    SDL_RenderCopy(sdl_renderer, texture, nullptr, &dst);
    break;
  }
  }

  if (screenshotDue) {
    captureDueScreenshots();
  }
  SDL_RenderPresent(sdl_renderer);
}

bool HalDisplay::shouldQuit() const { return quitRequested.load(); }

void HalDisplay::deepSleep() { presentIfNeeded(); }

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
void HalDisplay::displayGrayscaleBase(RefreshMode fallback,
                                      bool turnOffScreen) {
  if (einkDisplay.combinesGrayscaleBase()) {
    einkDisplay.snapshotBwBase();
    return;
  }
  displayBuffer(fallback, turnOffScreen);
}
void HalDisplay::preconditionGrayscale() {}
void HalDisplay::preconditionGrayscale(uint16_t, uint16_t, uint16_t, uint16_t) {
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
void HalDisplay::displayGrayBuffer(bool, const unsigned char *, bool) {
  const std::lock_guard<std::mutex> lock(pixelBufMutex);
  einkDisplay.composeGrayscaleArgb(pixelBuf, inverted);
  pendingPresent.store(true);
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
