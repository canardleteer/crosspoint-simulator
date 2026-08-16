#include "SimulatorDisplay.h"

#include <BoardConfig.h>
#include <EInkDisplay.h>
#include <GfxRenderer.h>
#include <SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Arduino.h"

#ifdef CROSSPOINT_SIM_GRPC
#include "sim_grpc/session_client.h"
#endif

static SDL_Window *window = nullptr;
static SDL_Renderer *sdl_renderer = nullptr;
static SDL_Texture *texture = nullptr;
static constexpr int SIMULATOR_WINDOW_SCALE = 1;

static uint32_t
    pixelBuf[BoardConfig::MAX_DISPLAY_WIDTH * BoardConfig::MAX_DISPLAY_HEIGHT];
static std::mutex pixelBufMutex;
static std::atomic<bool> pendingPresent{false};
static std::atomic<bool> quitRequested{false};

static int currentWindowWidth = 0;
static int currentWindowHeight = 0;

extern GfxRenderer renderer;

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

bool isPortraitOrientation(GfxRenderer::Orientation orientation) {
  return orientation == GfxRenderer::Portrait ||
         orientation == GfxRenderer::PortraitInverted;
}

void getLogicalWindowSize(GfxRenderer::Orientation orientation, int *width,
                          int *height) {
  const bool isPortrait = isPortraitOrientation(orientation);
  const int panelW = BoardConfig::ACTIVE.displayWidth;
  const int panelH = BoardConfig::ACTIVE.displayHeight;
  *width = (isPortrait ? panelH : panelW) * SIMULATOR_WINDOW_SCALE;
  *height = (isPortrait ? panelW : panelH) * SIMULATOR_WINDOW_SCALE;
}

void applyWindowGeometryIfNeeded(GfxRenderer::Orientation orientation) {
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

const char *windowTitle() {
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

} // namespace

namespace SimulatorDisplay {

void begin() {
  if (window)
    return;

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError()
              << std::endl;
    return;
  }

  int winW = 0;
  int winH = 0;
  getLogicalWindowSize(renderer.getOrientation(), &winW, &winH);

  window = SDL_CreateWindow(windowTitle(), SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, winW, winH,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_RenderSetLogicalSize(sdl_renderer, winW, winH);
  currentWindowWidth = winW;
  currentWindowHeight = winH;

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  texture = SDL_CreateTexture(
      sdl_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      BoardConfig::ACTIVE.displayWidth, BoardConfig::ACTIVE.displayHeight);
}

void scheduleBwPresent(const EInkDisplay &display) {
  const std::lock_guard<std::mutex> lock(pixelBufMutex);
  display.composeBwArgb(pixelBuf, display.isInverted());
  pendingPresent.store(true);
}

void scheduleGrayscalePresent(const EInkDisplay &display) {
  const std::lock_guard<std::mutex> lock(pixelBufMutex);
  display.composeGrayscaleArgb(pixelBuf, display.isInverted());
  pendingPresent.store(true);
}

#ifdef CROSSPOINT_SIM_GRPC
void captureSessionSnapshotIfRequested() {
  SimGrpc::SnapshotCaptureRequest req;
  if (!SimGrpc::consumeSnapshotRequest(&req))
    return;
  if (req.format != 0) {
    SimGrpc::finishSnapshotError(req.corr, "unknown_format");
    return;
  }

  const uint32_t panelW =
      static_cast<uint32_t>(BoardConfig::ACTIVE.displayWidth);
  const uint32_t panelH =
      static_cast<uint32_t>(BoardConfig::ACTIVE.displayHeight);
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t w = panelW;
  uint32_t h = panelH;
  if (req.region) {
    if (req.x >= panelW || req.y >= panelH || req.width == 0 ||
        req.height == 0) {
      SimGrpc::finishSnapshotError(req.corr, "empty_region");
      return;
    }
    x = req.x;
    y = req.y;
    w = req.width;
    h = req.height;
    if (x + w > panelW)
      w = panelW - x;
    if (y + h > panelH)
      h = panelH - y;
    if (w == 0 || h == 0) {
      SimGrpc::finishSnapshotError(req.corr, "empty_region");
      return;
    }
  }

  std::vector<uint8_t> gray(static_cast<size_t>(w) * h);
  {
    const std::lock_guard<std::mutex> lock(pixelBufMutex);
    for (uint32_t row = 0; row < h; ++row) {
      for (uint32_t col = 0; col < w; ++col) {
        const uint32_t argb = pixelBuf[(y + row) * panelW + (x + col)];
        gray[row * w + col] = static_cast<uint8_t>(argb & 0xFFu);
      }
    }
  }
  SimGrpc::finishSnapshotGray(req.corr, SimGrpc::framebufferGeneration(), w, h,
                              std::move(gray));
}
#endif

void presentIfNeeded() {
#ifdef CROSSPOINT_SIM_GRPC
  captureSessionSnapshotIfRequested();
#endif
  const bool screenshotDue = hasDueScreenshot();
  if (!pendingPresent.exchange(false) && !screenshotDue)
    return;

  if (!texture || !sdl_renderer)
    return;

  const GfxRenderer::Orientation orientation = renderer.getOrientation();
  applyWindowGeometryIfNeeded(orientation);

  const int panelW = BoardConfig::ACTIVE.displayWidth;
  const int panelH = BoardConfig::ACTIVE.displayHeight;
  {
    const std::lock_guard<std::mutex> lock(pixelBufMutex);
    SDL_UpdateTexture(texture, nullptr, pixelBuf,
                      panelW * static_cast<int>(sizeof(uint32_t)));
  }
  SDL_RenderClear(sdl_renderer);

  switch (orientation) {
  case GfxRenderer::Portrait: {
    SDL_Rect dst = {(panelH - panelW) / 2, panelW / 2 - panelH / 2, panelW,
                    panelH};
    SDL_RenderCopyEx(sdl_renderer, texture, nullptr, &dst, 90.0, nullptr,
                     SDL_FLIP_NONE);
    break;
  }
  case GfxRenderer::PortraitInverted: {
    SDL_Rect dst = {(panelH - panelW) / 2, panelW / 2 - panelH / 2, panelW,
                    panelH};
    SDL_RenderCopyEx(sdl_renderer, texture, nullptr, &dst, -90.0, nullptr,
                     SDL_FLIP_NONE);
    break;
  }
  case GfxRenderer::LandscapeClockwise: {
    SDL_Rect dst = {0, 0, panelW, panelH};
    SDL_RenderCopyEx(sdl_renderer, texture, nullptr, &dst, 180.0, nullptr,
                     SDL_FLIP_NONE);
    break;
  }
  default: {
    SDL_Rect dst = {0, 0, panelW, panelH};
    SDL_RenderCopy(sdl_renderer, texture, nullptr, &dst);
    break;
  }
  }

  if (screenshotDue) {
    captureDueScreenshots();
  }
  SDL_RenderPresent(sdl_renderer);
#ifdef CROSSPOINT_SIM_GRPC
  SimGrpc::bumpFramebufferGeneration();
#endif
}

void presentIfOnMainThread() {
  if (std::this_thread::get_id() == simulatorMainThread)
    presentIfNeeded();
}

bool shouldQuit() { return quitRequested.load(); }

void requestQuit() { quitRequested.store(true); }

} // namespace SimulatorDisplay
