# AGENTS.md

This file is a README for agents: the extra build, architecture, and convention context that helps coding agents work in this repository.

## What this repo is

A desktop simulator for [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader) firmware (the CrossPoint reader). It is **not** a standalone app, it ships as a PlatformIO library that downstream firmware adds as a `lib_dep` (named `simulator`) and builds with `platform = native` and `-DSIMULATOR`. The result is the firmware compiled as a host binary, with the e-ink display rendered into an SDL2 window.

There is no build target inside this repo. Build and run happen in the consuming firmware project, typically a checkout of [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader). See [README.md](README.md) for end-user setup, [FORKING.md](FORKING.md) when the consuming firmware's HAL diverges, and [docs/simulator-context.md](docs/simulator-context.md) for the deep architecture notes and bug-fix history (read this before non-trivial changes).

This library **replaces** the firmware HAL rather than extending it for GPIO, storage, power, and system. The consuming `[env:simulator]` still lists `hal` in `lib_ignore` so firmware `HalStorage.h` / `HalGPIO.h` stay off the include path. The extra script compiles firmware display, frontlight, tilt, and clock TUs against host `EInkDisplay` / `FrontlightManager` / `Imu` / `Rtc`. Consumers without those firmware files keep the host copies under `src/compat_hal/`. Editing `.pio/libdeps/simulator/` is not a fork; PlatformIO will wipe it.

## Faithfulness

This is a **host twin of the firmware**, not a hardware emulator. The same firmware sources run on the desktop. What you can trust, and what you must not infer from a sim run:

**Faithful enough for UI and file-flow work**

- Firmware logic, menus, indexing, and caches are the real code. `/books/` on the SD card is `./fs_/books/` under the binary's working directory.
- Device profiles report the same framebuffer geometry and capability flags as the matching production board (`BoardConfig.h`). The selected controller appears in the window title.
- Buttons, Home, and touch/swipe go through the same `HalGPIO` state as on device.
- Firmware-owned HTTP/WebDAV/WebSocket routes run against the host shims. OPDS and KOReader sync use the host `curl` binary (or `CROSSPOINT_SIM_HTTP_MOCK_ROOT` fixtures) while still exercising firmware parse/download/write paths.

**Host stand-ins (same API, different physics)**

- The SDL window is the 1bpp framebuffer uploaded on the main thread. It does **not** model e-ink controller timing, LUT waveforms, ghosting, or power sequencing.
- JPEG/PNG paths are desktop previews (default `stb_image`, or opt-in native decoders). Neither matches device image quality, refresh behaviour, or memory pressure.
- `millis()` / `micros()` are host `steady_clock`. FreeRTOS tasks are `std::thread`. Deep sleep is a process relaunch plus a synthetic power-button wake, not ESP sleep/resume.
- Firmware binds of port 80/81 are exposed on `8080`/`8081` (or the `CROSSPOINT_SIM_HTTP_PORT` pair). Reported heap is a host override (1 MiB default), not ESP SRAM.
- Serial/`LOG_*` go to stderr.

**Intentionally non-faithful**

- A new HAL or Arduino/ESP-IDF symbol is often a one-line no-op until someone implements behaviour. Linking is not proof the call does something.
- OTA and SD-card firmware flashing are non-destructive stubs so those UIs can open without writing partitions.
- WebDAV `LOCK` / `UNLOCK` are compatibility-only unless the firmware implements locking.
- The UC8279 X4 Pro profile mirrors current FreeInk SDK flags but is pending validation on physical UC8279 X4 Pro hardware.

Do not treat a clean simulator run as proof of e-ink waveforms, power draw, heap exhaustion, or boot-partition updates. See [README.md](README.md) for the human-facing notes this list is drawn from.

## Build and run (from the consuming firmware repo)

```bash
pio run -e simulator -t run_simulator   # build + launch
pio run -e simulator                    # build only, then .pio/build/simulator/program
rm -rf ./fs_/.crosspoint/               # clear stale on-disk caches after storage/cache changes
```

For local dev against this repo, the firmware's `platformio.ini` should reference it as `simulator=symlink://../crosspoint-simulator` instead of the git URL.

There are no tests, no linter, and no per-file build commands. A change is "tested" by running the simulator and exercising the affected feature.

## Architecture

The simulator is a collection of host-side reimplementations of the firmware's hardware abstraction layer (HAL) and its Arduino/ESP-IDF dependencies. Each `Hal*.cpp/.h` here corresponds to a `Hal*` class in the firmware's `lib/hal/`, and **must keep the same public surface** or the firmware will not link.

**The HAL stub rule.** When the firmware adds a new method to a HAL class and calls it, the simulator fails to link until a matching stub is added to the corresponding `Hal*.cpp` here. Most additions are one-line no-ops. This is the single most common reason a simulator build breaks after pulling firmware updates.

**Why the simulator's design has the shape it does** (the non-obvious parts):

- **SDL on main thread.** Firmware drives rendering from a FreeRTOS render task. The split lives in [src/EInkDisplay.cpp](src/EInkDisplay.cpp) and [src/SimulatorDisplay.cpp](src/SimulatorDisplay.cpp): `refreshDisplay` (background thread) converts the 1bpp framebuffer to ARGB and sets an atomic `pendingPresent` flag. `SimulatorDisplay::presentIfNeeded` (called from `simulator_main` on the main thread) does the actual SDL upload and present. Do not call SDL render functions from anywhere else.
- **Orientation rotation lives in two places.** The firmware's renderer rotates content into the landscape framebuffer (90 CCW for `Portrait`). The simulator undoes that with `SDL_RenderCopyEx`. If you change one, change the other. The dst rect is landscape-shaped and centre-offset because `SDL_RenderCopyEx` rotates around the dst centre.
- **HiDPI / dithering.** Set `SDL_HINT_RENDER_SCALE_QUALITY=1` *before* `SDL_CreateTexture`, plus `SDL_WINDOW_ALLOW_HIGHDPI` and `SDL_RenderSetLogicalSize`. Keep all three.
- **POSIX fds, not std::fstream, in [src/HalStorage.cpp](src/HalStorage.cpp).** This was a deliberate rewrite. fstream's separate get/put pointers, eofbit-blocks-seek behaviour, and write-only seek restrictions caused several silent-corruption bugs. Do not reintroduce fstream here. All paths are prefixed with `./fs_` so the simulated filesystem stays sandboxed under the binary's working directory; `/books/` on the SD card maps to `./fs_/books/`. Directory iteration skips only the special `.` and `..` entries; firmware applies its own hidden-file policy.
- **FreeRTOS shim.** [src/freertos/](src/freertos/) maps `xTaskCreate` to `std::thread`, task notifies to a condvar + counter, and `SemaphoreHandle_t` to `std::recursive_mutex`. A `thread_local SimTaskHandle*` lets each task thread find its own handle.
- **`_exit(0)` not `return 0`.** [src/simulator_main.cpp](src/simulator_main.cpp) ends with `_exit(0)` after `SDL_Quit()` to skip C++ global destructors. The render task is `[[noreturn]]`, so running destructors while it is mid-render races and produces a "quit unexpectedly" dialog. Keep this.
- **Time uses `steady_clock`.** `millis()` / `micros()` in [src/Arduino.h](src/Arduino.h) deliberately use `steady_clock`, not `system_clock`, so wall-clock changes do not perturb timing.

**Host-specific code paths:**

- Host deps, MD5 backends, sample PlatformIO ini selection, and native
  Windows support live in the `host-platforms` skill at
  [.agents/skills/host-platforms/](.agents/skills/host-platforms/). Load
  that skill when changing those paths.
- Web server shims: [src/WebServer.cpp](src/WebServer.cpp), [src/WebSocketsServer.cpp](src/WebSocketsServer.cpp), and [src/NetworkClient.cpp](src/NetworkClient.cpp) expose firmware port 80 as `http://127.0.0.1:8080/` and port 81 WebSockets as `ws://127.0.0.1:8081/`. `CROSSPOINT_SIM_HTTP_PORT` moves the pair together when either port is occupied. Current CrossPoint builds compile their firmware-owned `CrossPointWebServer.cpp` and `WebDAVHandler.cpp` against these shims; `CROSSPOINT_SIMULATOR_PROJECT_WEBSERVER` disables only the legacy reduced substitute in this library.
- Linker stubs: [src/firmware_link_stubs.cpp](src/firmware_link_stubs.cpp) provides symbols the firmware expects from other translation units (uzlib checksums, HWCDC Serial shim, LUT stubs). When the firmware adds a new global-extern symbol with no simulator counterpart, add its stub here.

## Device profiles and input mapping

Host `BoardConfig`, `EInkDisplay`, and `InputManager` track the FreeInk SDK
surface. Device selection is `-DFREEINK_DEVICE_*` (same as firmware).
[src/BoardConfig.h](src/BoardConfig.h) defaults to X4 when no device flag is
set. `SIMULATOR_DISPLAY_UC8179` and `SIMULATOR_DISPLAY_UC8279` remain host-only
controller overrides (no probe). Older `SIMULATOR_DEVICE_*` flags are
non-overwriting shims only. `Hal*` here stays a thin compatibility layer until
firmware compiles its own HAL. Keep the reported board and controller aligned
with the firmware SDK. X4 Pro uses the same 800x480 display geometry as X4 but
adds touch, a capacitive Home key, frontlight state, inversion, and an RTC.
Sticky also uses 800x480 and adds touch, RTC, and tilt without a Home key or
frontlight. Paper Mono uses 800x480 with FT5x06 touch, a PMIC frontlight, and
combined grayscale presents.

`HalGPIO::update` owns the SDL event pump for the whole simulator, do not poll SDL events elsewhere. Scancodes map to button indices `BTN_BACK=0` through `BTN_POWER=6`. `SDL_QUIT` sets the quit flag that `SimulatorDisplay::shouldQuit()` reads.

For repeatable QA, `CROSSPOINT_SIM_INPUT_SCRIPT` schedules synthetic key and
touch-device edges through the same `HalGPIO` state as real SDL input, and
`CROSSPOINT_SIM_SCREENSHOTS` captures renderer output on the SDL main thread.
Keep synthetic held-time timestamps on the `SDL_GetTicks()` clock used by real
keyboard events; the firmware's `millis()` clock has a different origin. The
deep-sleep loop must also process synthetic input. Process relaunch promotes
the optional `*_AFTER_WAKE` schedules and clears the pre-sleep schedules so
automation cannot enter an infinite sleep/relaunch cycle.

## When making changes

- Adding a new HAL method? Mirror the firmware signature exactly and stub it (usually no-op) in the matching `Hal*.cpp/.h`. Do not invent new public methods that don't exist in the firmware HAL. Keep the diff small and additive; do not reformat a `Hal*.cpp` file as a drive-by.
- Adding a new Arduino/ESP-IDF symbol? Add the minimum stub to the corresponding header in [src/](src/) (e.g. [src/WiFi.h](src/WiFi.h), [src/Arduino.h](src/Arduino.h)). Match the upstream signature, return a sensible default.
- Touching storage or caching code? After the change, `rm -rf ./fs_/.crosspoint/` in the firmware project before re-running, otherwise stale caches built by the old code will mask the fix.
- Touching display, threading, or shutdown? Re-read the "Why the simulator's design has the shape it does" section above first. Several of those decisions undo subtle bugs that will resurface if reverted.

If the change is about **what the simulator does** (Arduino/ESP-IDF gaps, rendering, storage, threading, input, web shims, host portability), it belongs here. If it is about **what a firmware fork's HAL looks like** (add/remove/change a `Hal*` method, or device profiles for hardware this project does not target), keep it in a fork of this repo and point the firmware `lib_deps` at that fork. See [FORKING.md](FORKING.md). Once a fork-only HAL change lands in [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader), track it here and drop the fork patch.

## Session client

How the client is enabled, what it sends and accepts, and how it
hooks the HAL is in
[docs/grpc-client.md](docs/grpc-client.md) (alpha). Durable compile
and history rules are in this section. `.proto` files live in
[crosspoint-simulator-mcp](https://github.com/canardleteer/crosspoint-simulator-mcp).

An optional grpc++ client can dial a host `Session` listener (plaintext
gRPC). Default simulator builds must stay unchanged: do not compile or
link this client unless `-DCROSSPOINT_SIM_GRPC` is set. When compiled
in, dialing is still opt-in at startup. Do not write RPC bytes to
stdout. Linux is the only current test host.

All Session client C++ lives under [src/sim_grpc/](src/sim_grpc/).
Generated protobuf and grpc++ stubs are deposited in
[src/sim_grpc/gen/](src/sim_grpc/gen/); hand-written client code sits
beside them. This tree does not contain `.proto` files and must
not require them to build. Do not add ConnectRPC, Connect, or gRPC-Web
here.

Use a dedicated worker thread. Never call RPC from `simulator_main`,
`SimulatorDisplay::presentIfNeeded`, or `HalGPIO::update`. Drain remote
injects on the same path as existing synthetic input. `InputAck` is sent
only when the host set `ack_requested`; do not invent acks. Snapshot
capture stays on the SDL main thread; encode off that thread.
`Register.board_id` is `x4`, `x3`, `x4_pro`, `sticky`, or `paper_mono`.
`cap_frontlight` follows `BoardConfig::hasFrontlight()` (any style,
including Paper Mono PMIC).

`--sim-headless` or `CROSSPOINT_SIM_HEADLESS=1` (same truthy rule as
`CROSSPOINT_SIM_GRPC`) hides the SDL window and drops local keyboard,
mouse, home, sleep, and `SDL_QUIT`. Keep polling so the queue does not
stall; keep applying remote and script injects. Do not `setenv` the
flag (`SimulatorLifecycle::rebootAsPowerWake` uses `execvp`). Report
`Heartbeat.headless`. A headless process exits on `ShutdownRequest`.

Bugs that precede commit `92520e1` are separate commits so they can
rebase upstream, not only on this client branch.

## Agent Documentation Standards

Project-local skills exist under `.agents/skills/` and should remain
discoverable by agents working in this repository. Maintain those skills
according to the [Agent Skills specification](https://agentskills.io/specification),
and maintain this file according to the
[AGENTS.md standard](https://agents.md/). Keep both portable
across compatible agent clients, without assumptions about user-specific paths
or session state.
