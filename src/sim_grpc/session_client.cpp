#ifdef CROSSPOINT_SIM_GRPC

#include "session_client.h"

#include "sha256.h"
#include "snapshot_png.h"

#include "BoardConfig.h"
#include "HalDisplay.h"

#include "crosspoint/sim/control/v1alpha1/session.grpc.pb.h"
#include "crosspoint/sim/control/v1alpha1/session.pb.h"
#include "crosspoint/sim/control/v1alpha1/simulator_control.pb.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern std::atomic<bool> quitRequested;

namespace SimGrpc {
namespace {

using crosspoint::sim::control::v1alpha1::Goodbye;
using crosspoint::sim::control::v1alpha1::Heartbeat;
using crosspoint::sim::control::v1alpha1::HomeEdge;
using crosspoint::sim::control::v1alpha1::HostEdge;
using crosspoint::sim::control::v1alpha1::InputAck;
using crosspoint::sim::control::v1alpha1::InputObserved;
using crosspoint::sim::control::v1alpha1::InputSource;
using crosspoint::sim::control::v1alpha1::KeyEdge;
using crosspoint::sim::control::v1alpha1::LogLine;
using crosspoint::sim::control::v1alpha1::LogSeverity;
using crosspoint::sim::control::v1alpha1::LogType;
using crosspoint::sim::control::v1alpha1::Register;
using crosspoint::sim::control::v1alpha1::ServerToSim;
using crosspoint::sim::control::v1alpha1::SimToServer;
using crosspoint::sim::control::v1alpha1::SimulatorControlService;
using crosspoint::sim::control::v1alpha1::SnapshotError;
using crosspoint::sim::control::v1alpha1::SnapshotFrame;
using crosspoint::sim::control::v1alpha1::TouchEdge;

constexpr size_t kQueueCap = 32;
constexpr int kIdMin = 1;
constexpr int kIdMax = 64;
constexpr unsigned long kDefaultHoldMs = 80;
constexpr unsigned long kDefaultSwipeMs = 250;

std::atomic<bool> gStop{false};
std::atomic<bool> gStarted{false};
std::atomic<uint64_t> gFrameGen{0};
std::atomic<uint64_t> gSeq{1};
std::atomic<uint64_t> gLogSeq{1};
std::atomic<bool> gInjectEnabled{true};
std::thread gWorker;

std::mutex gQueueMu;
std::deque<SimToServer> gQueue;

std::mutex gInjectMu;
std::deque<std::vector<RemoteEvent>> gInjectQ;

std::mutex gViewMu;
std::vector<std::string> gReadMask;

std::mutex gSnapMu;
bool gSnapPending = false;
bool gSnapReady = false;
SnapshotCaptureRequest gSnapReq;
uint64_t gSnapCorr = 0;
uint64_t gSnapGen = 0;
uint32_t gSnapWidth = 0;
uint32_t gSnapHeight = 0;
std::vector<uint8_t> gSnapGray;
std::string gSnapError;

std::mutex gReasonMu;
std::string gGoodbyeReason = "quit";

std::mutex gFwLogMu;
std::string gFwLogLine;

const char *boardId() {
  switch (BoardConfig::ACTIVE.board) {
  case BoardConfig::Board::XteinkX3:
  case BoardConfig::Board::XteinkX3Uc8279:
    return "x3";
  case BoardConfig::Board::XteinkX4Pro:
    return "x4_pro";
  case BoardConfig::Board::Sticky:
    return "sticky";
  case BoardConfig::Board::XteinkX4:
  default:
    return "x4";
  }
}

const char *controllerId() {
  switch (BoardConfig::ACTIVE.displayController) {
  case BoardConfig::DisplayController::UC8179:
    return "uc8179";
  case BoardConfig::DisplayController::UC8279:
    return "uc8279";
  case BoardConfig::DisplayController::UC8253:
    return "uc8253";
  case BoardConfig::DisplayController::SSD1677:
  default:
    return "ssd1677";
  }
}

std::string generateInstanceId() {
  unsigned char bytes[16] = {};
  std::ifstream urandom("/dev/urandom", std::ios::binary);
  if (urandom) {
    urandom.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
  }
  static const char *hex = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out.push_back('-');
    }
    out.push_back(hex[(bytes[i] >> 4) & 0xf]);
    out.push_back(hex[bytes[i] & 0xf]);
  }
  return out;
}

bool envEnabled() {
  const char *value = std::getenv("CROSSPOINT_SIM_GRPC");
  return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

const char *envOrNull(const char *name) {
  const char *value = std::getenv(name);
  if (!value || value[0] == '\0') {
    return nullptr;
  }
  return value;
}

void enqueue(SimToServer msg, bool drop_oldest) {
  if (!gStarted.load()) {
    return;
  }
  std::lock_guard<std::mutex> lock(gQueueMu);
  if (gQueue.size() == kQueueCap) {
    if (!drop_oldest) {
      return;
    }
    gQueue.pop_front();
  }
  gQueue.push_back(std::move(msg));
}

bool dequeue(SimToServer *out) {
  std::lock_guard<std::mutex> lock(gQueueMu);
  if (gQueue.empty()) {
    return false;
  }
  *out = std::move(gQueue.front());
  gQueue.pop_front();
  return true;
}

bool alwaysEmit(const char *name) {
  return std::strcmp(name, "register") == 0 ||
         std::strcmp(name, "goodbye") == 0 ||
         std::strcmp(name, "input_ack") == 0 ||
         std::strcmp(name, "snapshot") == 0 ||
         std::strcmp(name, "snapshot_error") == 0;
}

bool shouldEmit(const char *name) {
  if (alwaysEmit(name)) {
    return true;
  }
  std::lock_guard<std::mutex> lock(gViewMu);
  if (gReadMask.empty()) {
    return true;
  }
  return std::find(gReadMask.begin(), gReadMask.end(), name) != gReadMask.end();
}

void setReadMask(const google::protobuf::RepeatedPtrField<std::string> &paths) {
  std::lock_guard<std::mutex> lock(gViewMu);
  gReadMask.assign(paths.begin(), paths.end());
}

void setGoodbyeReason(const char *reason) {
  std::lock_guard<std::mutex> lock(gReasonMu);
  gGoodbyeReason = reason;
}

std::string goodbyeReason() {
  std::lock_guard<std::mutex> lock(gReasonMu);
  return gGoodbyeReason;
}

uint32_t clampPanelX(uint32_t x) {
  const uint32_t max = HalDisplay::DISPLAY_WIDTH
                           ? static_cast<uint32_t>(HalDisplay::DISPLAY_WIDTH - 1)
                           : 0;
  return x > max ? max : x;
}

uint32_t clampPanelY(uint32_t y) {
  const uint32_t max =
      HalDisplay::DISPLAY_HEIGHT
          ? static_cast<uint32_t>(HalDisplay::DISPLAY_HEIGHT - 1)
          : 0;
  return y > max ? max : y;
}

int namedButton(const std::string &name) {
  if (name == "ESCAPE" || name == "BACK")
    return 0;
  if (name == "RETURN" || name == "ENTER" || name == "CONFIRM")
    return 1;
  if (name == "LEFT")
    return 2;
  if (name == "RIGHT")
    return 3;
  if (name == "UP")
    return 4;
  if (name == "DOWN")
    return 5;
  if (name == "P" || name == "POWER")
    return 6;
  return -1;
}

std::string uppercase(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

bool tryPushInject(std::vector<RemoteEvent> events) {
  std::lock_guard<std::mutex> lock(gInjectMu);
  if (gInjectQ.size() >= kQueueCap) {
    return false;
  }
  gInjectQ.push_back(std::move(events));
  return true;
}

void enqueueAck(uint64_t corr, bool accepted, const char *reason) {
  InputAck ack;
  ack.set_accepted(accepted);
  if (reason && reason[0] != '\0') {
    ack.set_reason(reason);
  }
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.set_corr(corr);
  msg.mutable_input_ack()->Swap(&ack);
  enqueue(std::move(msg), true);
}

void maybeAck(const ServerToSim &inbound, bool accepted, const char *reason) {
  if (!inbound.ack_requested()) {
    return;
  }
  enqueueAck(inbound.corr(), accepted, reason);
}

SimToServer makeRegister(const std::string &instance_id) {
  Register reg;
  reg.set_instance_id(instance_id);
  reg.set_board_id(boardId());
  reg.set_controller(controllerId());
  reg.set_width(HalDisplay::DISPLAY_WIDTH);
  reg.set_height(HalDisplay::DISPLAY_HEIGHT);
  reg.set_cap_touch(BoardConfig::hasTouch());
  reg.set_cap_home(BoardConfig::hasHomeKey());
  reg.set_cap_frontlight(BoardConfig::hasPwmFrontlight());
  reg.set_pid(static_cast<uint32_t>(getpid()));
#ifdef CROSSPOINT_VERSION
  reg.set_version(CROSSPOINT_VERSION);
#endif
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_register_()->Swap(&reg);
  return msg;
}

SimToServer makeHeartbeat() {
  Heartbeat hb;
  hb.set_framebuffer_generation(gFrameGen.load());
  hb.set_inject_enabled(gInjectEnabled.load());
  hb.set_headless(false);
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_heartbeat()->Swap(&hb);
  return msg;
}

SimToServer makeGoodbye() {
  Goodbye bye;
  bye.set_reason(goodbyeReason());
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_goodbye()->Swap(&bye);
  return msg;
}

void enqueueSnapshotError(uint64_t corr, const char *message) {
  SnapshotError err;
  err.set_message(message ? message : "");
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.set_corr(corr);
  msg.mutable_snapshot_error()->Swap(&err);
  enqueue(std::move(msg), true);
}

bool snapshotBusy() {
  std::lock_guard<std::mutex> lock(gSnapMu);
  return gSnapPending || gSnapReady;
}

void handleSnapshotRequest(const ServerToSim &inbound) {
  if (snapshotBusy()) {
    enqueueSnapshotError(inbound.corr(), "busy");
    return;
  }
  const auto &req = inbound.snapshot_request();
  std::lock_guard<std::mutex> lock(gSnapMu);
  gSnapPending = true;
  gSnapReady = false;
  gSnapError.clear();
  gSnapGray.clear();
  gSnapReq.corr = inbound.corr();
  gSnapReq.region = req.region();
  gSnapReq.x = req.x();
  gSnapReq.y = req.y();
  gSnapReq.width = req.width();
  gSnapReq.height = req.height();
  gSnapReq.format = req.format();
}

void handleTouch(const ServerToSim &inbound) {
  if (!gInjectEnabled.load()) {
    maybeAck(inbound, false, "inject_disabled");
    return;
  }
  if (!BoardConfig::hasTouch()) {
    maybeAck(inbound, false, "no_touch");
    return;
  }
  const auto &touch = inbound.inject_touch();
  const uint32_t x = clampPanelX(touch.x());
  const uint32_t y = clampPanelY(touch.y());
  std::vector<RemoteEvent> events;
  switch (touch.kind()) {
  case 0:
    events.push_back({0, RemoteAction::TouchDown, -1, x, y});
    break;
  case 1:
    events.push_back({0, RemoteAction::TouchMove, -1, x, y});
    break;
  case 2:
    events.push_back({0, RemoteAction::TouchUp, -1, x, y});
    break;
  case 3:
    events.push_back({0, RemoteAction::TouchDown, -1, x, y});
    events.push_back({kDefaultHoldMs, RemoteAction::TouchUp, -1, x, y});
    break;
  default:
    maybeAck(inbound, false, "unknown_kind");
    return;
  }
  if (!tryPushInject(std::move(events))) {
    maybeAck(inbound, false, "queue_full");
    return;
  }
  maybeAck(inbound, true, "");
}

void handleKey(const ServerToSim &inbound) {
  if (!gInjectEnabled.load()) {
    maybeAck(inbound, false, "inject_disabled");
    return;
  }
  const std::string name = uppercase(inbound.inject_key().name());
  const unsigned long hold = inbound.inject_key().hold_ms() == 0
                                 ? kDefaultHoldMs
                                 : inbound.inject_key().hold_ms();
  std::vector<RemoteEvent> events;
  if (name == "SLEEP" || name == "S") {
    events.push_back({0, RemoteAction::Sleep});
  } else if (name == "QUIT") {
    events.push_back({0, RemoteAction::Quit});
  } else {
    const int button = namedButton(name);
    if (button < 0) {
      maybeAck(inbound, false, "unknown_key");
      return;
    }
    events.push_back({0, RemoteAction::KeyDown, button});
    events.push_back({hold, RemoteAction::KeyUp, button});
  }
  if (!tryPushInject(std::move(events))) {
    maybeAck(inbound, false, "queue_full");
    return;
  }
  maybeAck(inbound, true, "");
}

void handleHome(const ServerToSim &inbound) {
  if (!gInjectEnabled.load()) {
    maybeAck(inbound, false, "inject_disabled");
    return;
  }
  if (!BoardConfig::hasHomeKey()) {
    maybeAck(inbound, false, "no_home");
    return;
  }
  const unsigned long hold = inbound.inject_home().hold_ms() == 0
                                 ? kDefaultHoldMs
                                 : inbound.inject_home().hold_ms();
  std::vector<RemoteEvent> events;
  events.push_back({0, RemoteAction::HomeDown});
  events.push_back({hold, RemoteAction::HomeUp});
  if (!tryPushInject(std::move(events))) {
    maybeAck(inbound, false, "queue_full");
    return;
  }
  maybeAck(inbound, true, "");
}

void handleSwipe(const ServerToSim &inbound) {
  if (!gInjectEnabled.load()) {
    maybeAck(inbound, false, "inject_disabled");
    return;
  }
  if (!BoardConfig::hasTouch()) {
    maybeAck(inbound, false, "no_touch");
    return;
  }
  const auto &swipe = inbound.inject_swipe();
  const uint32_t x1 = clampPanelX(swipe.start_x());
  const uint32_t y1 = clampPanelY(swipe.start_y());
  const uint32_t x2 = clampPanelX(swipe.end_x());
  const uint32_t y2 = clampPanelY(swipe.end_y());
  const unsigned long duration =
      swipe.duration_ms() == 0 ? kDefaultSwipeMs : swipe.duration_ms();
  const unsigned steps = std::max(2u, static_cast<unsigned>(duration / 16));
  std::vector<RemoteEvent> events;
  events.reserve(steps + 1);
  for (unsigned i = 0; i <= steps; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    const uint32_t x =
        static_cast<uint32_t>(static_cast<float>(x1) +
                              t * static_cast<float>(static_cast<int>(x2) -
                                                     static_cast<int>(x1)));
    const uint32_t y =
        static_cast<uint32_t>(static_cast<float>(y1) +
                              t * static_cast<float>(static_cast<int>(y2) -
                                                     static_cast<int>(y1)));
    const unsigned long at = duration * i / steps;
    if (i == 0) {
      events.push_back({at, RemoteAction::TouchDown, -1, x, y});
    } else if (i == steps) {
      events.push_back({at, RemoteAction::TouchUp, -1, x, y});
    } else {
      events.push_back({at, RemoteAction::TouchMove, -1, x, y});
    }
  }
  if (!tryPushInject(std::move(events))) {
    maybeAck(inbound, false, "queue_full");
    return;
  }
  maybeAck(inbound, true, "");
}

void handleInbound(const ServerToSim &inbound) {
  switch (inbound.payload_case()) {
  case ServerToSim::kInjectTouch:
    handleTouch(inbound);
    break;
  case ServerToSim::kInjectKey:
    handleKey(inbound);
    break;
  case ServerToSim::kInjectHome:
    handleHome(inbound);
    break;
  case ServerToSim::kInjectSwipe:
    handleSwipe(inbound);
    break;
  case ServerToSim::kSnapshotRequest:
    handleSnapshotRequest(inbound);
    break;
  case ServerToSim::kSetInjectEnabled:
    gInjectEnabled.store(inbound.set_inject_enabled().enabled());
    maybeAck(inbound, true, "");
    break;
  case ServerToSim::kShutdown:
    setGoodbyeReason("shutdown");
    quitRequested.store(true);
    maybeAck(inbound, true, "");
    break;
  case ServerToSim::kSetSessionView:
    setReadMask(inbound.set_session_view().read_mask().paths());
    maybeAck(inbound, true, "");
    break;
  case ServerToSim::PAYLOAD_NOT_SET:
    break;
  }
}

void flushCapturedSnapshot() {
  uint64_t corr = 0;
  uint64_t generation = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> gray;
  std::string error;
  {
    std::lock_guard<std::mutex> lock(gSnapMu);
    if (!gSnapReady) {
      return;
    }
    corr = gSnapCorr;
    generation = gSnapGen;
    width = gSnapWidth;
    height = gSnapHeight;
    gray.swap(gSnapGray);
    error.swap(gSnapError);
    gSnapReady = false;
  }
  if (!error.empty()) {
    enqueueSnapshotError(corr, error.c_str());
    return;
  }
  std::vector<uint8_t> png;
  if (!encodeGrayPng(gray.data(), static_cast<int>(width),
                     static_cast<int>(height), &png)) {
    enqueueSnapshotError(corr, "encode_failed");
    return;
  }
  SnapshotFrame frame;
  frame.set_pixels(png.data(), png.size());
  frame.set_mime_type("image/png");
  frame.set_hash(sha256Hex(png.data(), png.size()));
  frame.set_generation(generation);
  frame.set_width(width);
  frame.set_height(height);
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.set_corr(corr);
  msg.mutable_snapshot()->Swap(&frame);
  enqueue(std::move(msg), true);
}

void emitFirmwareLine(const std::string &line) {
  if (!gStarted.load() || line.empty() || !shouldEmit("log")) {
    return;
  }
  LogLine log;
  log.set_seq(gLogSeq.fetch_add(1));
  log.set_type(LogType::LOG_TYPE_FIRMWARE_SERIAL);
  log.set_severity(LogSeverity::LOG_SEVERITY_INFO);
  log.set_component("serial");
  log.set_text(line);
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_log()->Swap(&log);
  enqueue(std::move(msg), true);
}

InputSource observedSource(ObservedSource source) {
  switch (source) {
  case ObservedSource::Human:
    return InputSource::INPUT_SOURCE_HUMAN;
  case ObservedSource::Remote:
    return InputSource::INPUT_SOURCE_REMOTE;
  case ObservedSource::Script:
    return InputSource::INPUT_SOURCE_SCRIPT;
  }
  return InputSource::INPUT_SOURCE_UNSPECIFIED;
}

void enqueueObserved(InputObserved observed) {
  if (!shouldEmit("input_observed")) {
    return;
  }
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_input_observed()->Swap(&observed);
  enqueue(std::move(msg), true);
}

void runSession(const Options &opts) {
  auto channel =
      grpc::CreateChannel(opts.addr, grpc::InsecureChannelCredentials());
  const auto deadline =
      std::chrono::system_clock::now() + std::chrono::seconds(1);
  if (!channel->WaitForConnected(deadline)) {
    return;
  }
  auto stub = SimulatorControlService::NewStub(channel);
  grpc::ClientContext ctx;
  auto stream = stub->Session(&ctx);
  if (!stream) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(gQueueMu);
    gQueue.clear();
  }
  {
    std::lock_guard<std::mutex> lock(gInjectMu);
    gInjectQ.clear();
  }
  enqueue(makeRegister(opts.instance_id), false);

  std::thread reader([&]() {
    ServerToSim inbound;
    while (stream->Read(&inbound)) {
      handleInbound(inbound);
    }
  });

  auto last_hb = std::chrono::steady_clock::now();
  bool sent_register = false;
  while (!gStop.load()) {
    flushCapturedSnapshot();
    SimToServer outbound;
    if (dequeue(&outbound)) {
      if (!stream->Write(outbound)) {
        break;
      }
      if (outbound.has_register_()) {
        sent_register = true;
      }
    }
    const auto now = std::chrono::steady_clock::now();
    if (sent_register && now - last_hb >= std::chrono::seconds(1)) {
      if (shouldEmit("heartbeat")) {
        enqueue(makeHeartbeat(), true);
      }
      last_hb = now;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  if (sent_register) {
    stream->Write(makeGoodbye());
  }
  stream->WritesDone();
  ctx.TryCancel();
  reader.join();
  stream->Finish();
}

void workerMain(Options opts) {
  if (opts.instance_id.empty()) {
    opts.instance_id = generateInstanceId();
  }
  std::fprintf(stderr, "[SIM] control plane: dial %s id=%s\n",
               opts.addr.c_str(), opts.instance_id.c_str());
  emitLog(static_cast<int>(LogType::LOG_TYPE_CONTROL_PLANE),
          static_cast<int>(LogSeverity::LOG_SEVERITY_INFO), "control",
          "dial");

  bool logged_missing = false;
  int backoff_ms = 200;
  while (!gStop.load()) {
    runSession(opts);
    if (gStop.load()) {
      break;
    }
    if (!logged_missing) {
      std::fprintf(stderr, "[SIM] control plane: retrying %s\n",
                   opts.addr.c_str());
      emitLog(static_cast<int>(LogType::LOG_TYPE_CONTROL_PLANE),
              static_cast<int>(LogSeverity::LOG_SEVERITY_WARN), "control",
              "retrying");
      logged_missing = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
    if (backoff_ms < 5000) {
      backoff_ms *= 2;
    }
  }
}

} // namespace

bool isValidInstanceId(const std::string &id) {
  return id.size() >= static_cast<size_t>(kIdMin) &&
         id.size() <= static_cast<size_t>(kIdMax);
}

bool parseArgs(int argc, char **argv, Options *out) {
  Options opts;
  if (const char *addr = envOrNull("CROSSPOINT_SIM_GRPC_ADDR")) {
    opts.addr = addr;
  }
  if (const char *id = envOrNull("CROSSPOINT_SIM_INSTANCE_ID")) {
    opts.instance_id = id;
  }
  bool enabled = envEnabled();
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--sim-grpc") {
      enabled = true;
    } else if (arg == "--sim-grpc-addr" && i + 1 < argc) {
      opts.addr = argv[++i];
    } else if (arg.rfind("--sim-grpc-addr=", 0) == 0) {
      opts.addr = arg.substr(std::strlen("--sim-grpc-addr="));
    } else if (arg == "--sim-instance-id" && i + 1 < argc) {
      opts.instance_id = argv[++i];
    } else if (arg.rfind("--sim-instance-id=", 0) == 0) {
      opts.instance_id = arg.substr(std::strlen("--sim-instance-id="));
    }
  }
  if (!enabled) {
    return false;
  }
  *out = std::move(opts);
  return true;
}

void start(const Options &opts) {
  gStop.store(false);
  gStarted.store(true);
  gWorker = std::thread(workerMain, opts);
}

void requestStop() { gStop.store(true); }

void join() {
  if (gWorker.joinable()) {
    gWorker.join();
  }
}

void bumpFramebufferGeneration() { gFrameGen.fetch_add(1); }

uint64_t framebufferGeneration() { return gFrameGen.load(); }

void drainRemoteEvents(std::vector<RemoteEvent> *out) {
  if (!out) {
    return;
  }
  std::lock_guard<std::mutex> lock(gInjectMu);
  while (!gInjectQ.empty()) {
    auto &batch = gInjectQ.front();
    out->insert(out->end(), batch.begin(), batch.end());
    gInjectQ.pop_front();
  }
}

void enqueueKeyObserved(ObservedSource source, const char *name, bool down) {
  InputObserved observed;
  observed.set_source(observedSource(source));
  KeyEdge *edge = observed.mutable_key();
  edge->set_name(name ? name : "");
  edge->set_down(down);
  enqueueObserved(std::move(observed));
}

void enqueueTouchObserved(ObservedSource source, uint32_t kind, float nx,
                          float ny) {
  InputObserved observed;
  observed.set_source(observedSource(source));
  TouchEdge *edge = observed.mutable_touch();
  edge->set_kind(kind);
  edge->set_nx(nx);
  edge->set_ny(ny);
  enqueueObserved(std::move(observed));
}

void enqueueHomeObserved(ObservedSource source, bool down) {
  InputObserved observed;
  observed.set_source(observedSource(source));
  observed.mutable_home()->set_down(down);
  enqueueObserved(std::move(observed));
}

void enqueueHostObserved(ObservedSource source, uint32_t kind) {
  InputObserved observed;
  observed.set_source(observedSource(source));
  observed.mutable_host()->set_kind(kind);
  enqueueObserved(std::move(observed));
}

bool consumeSnapshotRequest(SnapshotCaptureRequest *out) {
  if (!out) {
    return false;
  }
  std::lock_guard<std::mutex> lock(gSnapMu);
  if (!gSnapPending) {
    return false;
  }
  *out = gSnapReq;
  gSnapPending = false;
  return true;
}

void finishSnapshotGray(uint64_t corr, uint64_t generation, uint32_t width,
                        uint32_t height, std::vector<uint8_t> gray) {
  std::lock_guard<std::mutex> lock(gSnapMu);
  gSnapCorr = corr;
  gSnapGen = generation;
  gSnapWidth = width;
  gSnapHeight = height;
  gSnapGray = std::move(gray);
  gSnapError.clear();
  gSnapReady = true;
}

void finishSnapshotError(uint64_t corr, const char *message) {
  std::lock_guard<std::mutex> lock(gSnapMu);
  gSnapCorr = corr;
  gSnapError = message ? message : "snapshot failed";
  gSnapGray.clear();
  gSnapReady = true;
}

void teeFirmwareBytes(const uint8_t *data, size_t size) {
  if (!gStarted.load() || !data || size == 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(gFwLogMu);
  for (size_t i = 0; i < size; ++i) {
    const char c = static_cast<char>(data[i]);
    if (c == '\n') {
      emitFirmwareLine(gFwLogLine);
      gFwLogLine.clear();
    } else if (c != '\r') {
      if (gFwLogLine.size() < 512) {
        gFwLogLine.push_back(c);
      }
    }
  }
}

void emitLog(int type, int severity, const char *component, const char *text) {
  if (!gStarted.load() || !shouldEmit("log")) {
    return;
  }
  LogLine log;
  log.set_seq(gLogSeq.fetch_add(1));
  log.set_type(static_cast<LogType>(type));
  log.set_severity(static_cast<LogSeverity>(severity));
  log.set_component(component ? component : "");
  log.set_text(text ? text : "");
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_log()->Swap(&log);
  enqueue(std::move(msg), true);
}

} // namespace SimGrpc

#endif
