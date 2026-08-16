
#include <SDL.h>
#include <cstdio>
#include <unistd.h>

#include "Arduino.h"
#include "HalGPIO.h"
#include "SimulatorDisplay.h"
#include "SimulatorLifecycle.h"

#ifdef CROSSPOINT_SIM_GRPC
#include "sim_grpc/session_client.h"
#endif

extern void setup();
extern void loop();

int main(int argc, char **argv) {
  SimulatorLifecycle::initProcessArgs(argv);
#ifdef CROSSPOINT_SIM_GRPC
  SimGrpc::Options grpc_opts;
  const bool grpc_enabled = SimGrpc::parseArgs(argc, argv, &grpc_opts);
  if (grpc_enabled) {
    if (!grpc_opts.instance_id.empty() &&
        !SimGrpc::isValidInstanceId(grpc_opts.instance_id)) {
      std::fprintf(stderr,
                   "[SIM] --sim-instance-id / CROSSPOINT_SIM_INSTANCE_ID "
                   "must be 1-64 bytes\n");
      return 1;
    }
    SimGrpc::start(grpc_opts);
  }
#endif
  setup();
  while (!SimulatorDisplay::shouldQuit()) {
    // Clear input edge latches once per frame. update() may be called many
    // times within loop(); edges must survive across those calls and only
    // reset here at the frame boundary.
    gpio.beginFrame();
    loop();
    // SDL must be driven from the main thread on macOS.
    // The render task writes pixels and sets pendingPresent; we flush them
    // here.
    SimulatorDisplay::presentIfNeeded();
    // Yield to the OS so macOS delivers pending keyboard/window events to SDL.
    // Without this, the tight spin-loop starves the Cocoa event system and key
    // presses are only picked up sporadically. 1 ms also caps the loop at ~1
    // kHz, which matches realistic device behaviour (the real ESP32-C3 is
    // limited by FreeRTOS tick rate and e-ink refresh time).
    SDL_Delay(1);
  }
#ifdef CROSSPOINT_SIM_GRPC
  if (grpc_enabled) {
    SimGrpc::requestStop();
    SimGrpc::join();
  }
#endif
  SDL_Quit();
  // Use _exit() instead of return/exit() to bypass C++ global destructors.
  // `activityManager` (and other globals in main.cpp) are constructed before
  // the render task thread starts, and the render task runs a [[noreturn]]
  // infinite loop.  If normal exit() runs global destructors while the render
  // thread is mid-render, the destructor races with the thread → SIGABRT/
  // SIGSEGV → "quit unexpectedly" dialog.  SDL is already torn down above, so
  // calling _exit(0) here is safe.
  _exit(0);
}
