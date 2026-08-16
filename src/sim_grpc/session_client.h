#pragma once

#ifdef CROSSPOINT_SIM_GRPC

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace SimGrpc {

struct Options {
  std::string addr = "127.0.0.1:50051";
  std::string instance_id;
};

enum class ObservedSource {
  Human,
  Remote,
  Script,
};

enum class RemoteAction {
  KeyDown,
  KeyUp,
  TouchDown,
  TouchMove,
  TouchUp,
  HomeDown,
  HomeUp,
  Sleep,
  Quit,
};

struct RemoteEvent {
  unsigned long delay_ms = 0;
  RemoteAction action = RemoteAction::Quit;
  int button = -1;
  uint32_t panel_x = 0;
  uint32_t panel_y = 0;
};

struct SnapshotCaptureRequest {
  uint64_t corr = 0;
  bool region = false;
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
};

// True when --sim-grpc or CROSSPOINT_SIM_GRPC=1 is set. Fills *out.
bool parseArgs(int argc, char **argv, Options *out);

// Validate instance_id: 1-64 bytes. Empty means generate later.
bool isValidInstanceId(const std::string &id);

void start(const Options &opts);
void requestStop();
void join();

void bumpFramebufferGeneration();
uint64_t framebufferGeneration();
bool headless();

// Drain accepted remote injects onto the synthetic-input path (SDL thread).
void drainRemoteEvents(std::vector<RemoteEvent> *out);

void enqueueKeyObserved(ObservedSource source, const char *name, bool down);
void enqueueTouchObserved(ObservedSource source, uint32_t kind, float nx,
                          float ny);
void enqueueHomeObserved(ObservedSource source, bool down);
void enqueueHostObserved(ObservedSource source, uint32_t kind);

// Snapshot: consume on the SDL main thread; finish without encoding there.
bool consumeSnapshotRequest(SnapshotCaptureRequest *out);
void finishSnapshotGray(uint64_t corr, uint64_t generation, uint32_t width,
                        uint32_t height, std::vector<uint8_t> gray);
void finishSnapshotError(uint64_t corr, const char *message);

void teeFirmwareBytes(const uint8_t *data, size_t size);
void emitLog(int type, int severity, const char *component, const char *text);

} // namespace SimGrpc

#endif
