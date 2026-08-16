#ifdef CROSSPOINT_SIM_GRPC

#include "session_client.h"

#include "BoardConfig.h"
#include "HalDisplay.h"

#include "crosspoint/sim/control/v1alpha1/session.grpc.pb.h"
#include "crosspoint/sim/control/v1alpha1/session.pb.h"
#include "crosspoint/sim/control/v1alpha1/simulator_control.pb.h"

#include <grpcpp/grpcpp.h>

#include <atomic>
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

namespace SimGrpc {
namespace {

using crosspoint::sim::control::v1alpha1::Goodbye;
using crosspoint::sim::control::v1alpha1::Heartbeat;
using crosspoint::sim::control::v1alpha1::Register;
using crosspoint::sim::control::v1alpha1::ServerToSim;
using crosspoint::sim::control::v1alpha1::SimToServer;
using crosspoint::sim::control::v1alpha1::SimulatorControlService;

constexpr size_t kQueueCap = 32;
constexpr int kIdMin = 1;
constexpr int kIdMax = 64;

std::atomic<bool> gStop{false};
std::atomic<uint64_t> gFrameGen{0};
std::atomic<uint64_t> gSeq{1};
std::thread gWorker;

std::mutex gQueueMu;
std::deque<SimToServer> gQueue;

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
  hb.set_inject_enabled(true);
  hb.set_headless(false);
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_heartbeat()->Swap(&hb);
  return msg;
}

SimToServer makeGoodbye() {
  Goodbye bye;
  bye.set_reason("quit");
  SimToServer msg;
  msg.set_seq(gSeq.fetch_add(1));
  msg.mutable_goodbye()->Swap(&bye);
  return msg;
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
  enqueue(makeRegister(opts.instance_id), false);

  std::thread reader([&]() {
    ServerToSim inbound;
    while (stream->Read(&inbound)) {
      // First slice: drain and drop. Do not apply injects or acks.
    }
  });

  auto last_hb = std::chrono::steady_clock::now();
  bool sent_register = false;
  while (!gStop.load()) {
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
      enqueue(makeHeartbeat(), true);
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

} // namespace SimGrpc

#endif
