#pragma once

class EInkDisplay;

// Host-only SDL present path. Firmware HalDisplay has no presentIfNeeded;
// the main thread calls these instead of a Hal* method.
namespace SimulatorDisplay {

void begin();
void presentIfNeeded();
void presentIfOnMainThread();
bool shouldQuit();
void requestQuit();

void scheduleBwPresent(const EInkDisplay &display);
void scheduleGrayscalePresent(const EInkDisplay &display);

} // namespace SimulatorDisplay
