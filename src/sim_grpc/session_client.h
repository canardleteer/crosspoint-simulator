#pragma once

#ifdef CROSSPOINT_SIM_GRPC

#include <cstdint>
#include <string>

namespace SimGrpc {

struct Options {
  std::string addr = "127.0.0.1:50051";
  std::string instance_id;
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

} // namespace SimGrpc

#endif
