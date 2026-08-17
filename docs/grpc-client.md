# Session gRPC client (alpha)

This is **alpha**. The client works on Linux against a host
`Session` listener. The wire contract, host process, and MCP tools
can still change. Default simulator builds do not compile or link
this code.

The simulator is **not** an MCP peer. It is a grpc++ client that
dials `SimulatorControlService.Session`. A host such as
[crosspoint-simulator-mcp](https://github.com/canardleteer/crosspoint-simulator-mcp)
accepts that stream and may expose it over MCP.

Durable compile, transport, and history rules still apply. They are
summarized in this repository's [`AGENTS.md`](../AGENTS.md#session-client).

## What talks to what

```
firmware `program`  --grpc++ Session (plaintext)-->  host Session listener
     (this repo)                                      (MCP repo, or any
                                                       compatible server)
```

The firmware binary is built in a consuming project (typically
[crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader))
with this library as `lib_deps` `simulator`. Adding
`-DCROSSPOINT_SIM_GRPC` compiles the client into that binary. Dialing
is still opt-in at startup.

`.proto` files are **not** in this tree. The source of truth is
[crosspoint-simulator-mcp](https://github.com/canardleteer/crosspoint-simulator-mcp)
(`protos/crosspoint/sim/control/v1alpha1/`). That repository deposits
generated protobuf and grpc++ stubs here under
[src/sim_grpc/gen/](../src/sim_grpc/gen/). This repository must not
require the `.proto` files to build. Do not add ConnectRPC, Connect,
or gRPC-Web here.

## Enablement

Two gates, both required for a live session:

1. **Compile.** Define `-DCROSSPOINT_SIM_GRPC` in the firmware
   `[env:simulator]` `build_flags`. [run_simulator.py](../run_simulator.py)
   then `pkg-config`s `grpc++` and `protobuf`, adds
   `src/sim_grpc/gen` to the include path, and compiles the deposited
   stubs. The deposited stubs expect **protobuf 35** and **grpc++
   1.83**. Distro packages older than that will fail to compile.
2. **Runtime.** Pass `--sim-grpc` or set `CROSSPOINT_SIM_GRPC` to a
   truthy value (non-empty and not `0`). Without that, the binary
   starts as a normal SDL simulator and never opens a channel.

Do not write RPC or log bytes to stdout. Session logs and firmware
serial go to **stderr**.

Linux is the only current test host.

### Flags and environment

A flag is truthy when it is non-empty and not `0` (same rule as
`CROSSPOINT_SIM_GRPC`).

| Flag | Environment | Role |
| --- | --- | --- |
| `--sim-grpc` | `CROSSPOINT_SIM_GRPC` | Opt in to dialing |
| `--sim-grpc-addr` / `--sim-grpc-addr=` | `CROSSPOINT_SIM_GRPC_ADDR` | Host listen address (default `127.0.0.1:50051`) |
| `--sim-instance-id` / `--sim-instance-id=` | `CROSSPOINT_SIM_INSTANCE_ID` | Instance id, 1–64 bytes. Empty generates a UUID |
| `--sim-headless` | `CROSSPOINT_SIM_HEADLESS` | Hidden window; drop local human SDL input |

`parseArgs` records headless even when dialing is off, then returns
whether grpc is enabled. Do **not** `setenv` these flags.
`SimulatorLifecycle::rebootAsPowerWake` uses `execvp` with the
original argv; a `setenv` would not survive that path the way argv
and a real inherited environment do.

If `--sim-instance-id` / `CROSSPOINT_SIM_INSTANCE_ID` is set and is
not 1–64 bytes, the process exits before `setup()`.

### Example

Build the firmware `program` with `-DCROSSPOINT_SIM_GRPC` and a
`PKG_CONFIG_PATH` that finds protobuf 35 / grpc++ 1.83. Then:

```bash
./.pio/build/simulator/program \
  --sim-grpc \
  --sim-grpc-addr 127.0.0.1:50051 \
  --sim-instance-id my-sim \
  --sim-headless
```

A host must already be listening on that address, or the client
retries (see [Reconnect](#reconnect-and-missing-peers)).

## Layout

All Session C++ lives under [src/sim_grpc/](../src/sim_grpc/):

| Path | Role |
| --- | --- |
| `session_client.h` / `session_client.cpp` | Parse flags, worker, queues, inbound handlers |
| `snapshot_png.*` | Off-thread 8-bit gray PNG encode |
| `sha256.*` | Snapshot `hash` (hex SHA-256 of PNG bytes) |
| `gen/` | Deposited protobuf + grpc++ stubs (committed) |

HAL hooks stay in the existing files, behind `#ifdef CROSSPOINT_SIM_GRPC`:

- [src/simulator_main.cpp](../src/simulator_main.cpp) — parse, `start` / `join`
- [src/HalGPIO.cpp](../src/HalGPIO.cpp) — drain remote injects; emit
  `InputObserved`; skip human events when headless
- [src/SimulatorDisplay.cpp](../src/SimulatorDisplay.cpp) — hidden
  window; copy `pixelBuf` on the SDL thread when a snapshot is pending
- [src/HardwareSerial.h](../src/HardwareSerial.h) — tee `Serial` bytes
  as `LogLine` after the worker has started

## How a session runs

`simulator_main` calls `SimGrpc::parseArgs`, then `SimGrpc::start`,
then firmware `setup()`. `start` launches one worker thread. That
thread owns the channel. `simulator_main`,
`SimulatorDisplay::presentIfNeeded`, and `HalGPIO::update` must not
call RPC.

### Outbound (`SimToServer`)

On connect the worker sends `Register` first:

- `instance_id` (configured or generated)
- `board_id` / `controller` from `BoardConfig`
  (`x4`, `x3`, `x4_pro`, `sticky`, `paper_mono`)
- panel `width` / `height` from the **active** board profile
- `cap_touch`, `cap_home`, `cap_frontlight`
  (`cap_frontlight` is `BoardConfig::hasFrontlight()`: any style,
  including Paper Mono's PMIC, not PWM-only)
- `pid`, and `version` when `CROSSPOINT_VERSION` is defined

Then, about once a second after register, `Heartbeat` with
`framebuffer_generation`, `inject_enabled`, and `headless`.

On a clean stop it sends `Goodbye` (`reason` is `shutdown` when the
host sent `ShutdownRequest`). Bounded queues (capacity 32) sit
between the HAL/firmware threads and the worker. A full outbound
queue drops the oldest message so the firmware frame does not block.

### Inbound (`ServerToSim`)

A reader thread on the same stream calls `handleInbound`. Accepted
injects are pushed onto the same synthetic-input queue that
`CROSSPOINT_SIM_INPUT_SCRIPT` uses. `HalGPIO::update` (and the
deep-sleep loop) drain that queue on the SDL thread via
`processSyntheticEvents()`.

`InputAck` is sent **only** when the host set `ack_requested`. Ack
happens when the inject is accepted onto the queue, not after the
firmware applies it. Do not invent acks.

| Reject reason | When |
| --- | --- |
| `inject_disabled` | `SetInjectEnabled.enabled` is false |
| `queue_full` | Inject queue is at capacity |
| `unknown_key` | Key name is not mapped |
| `unknown_kind` | Touch `kind` is not 0–3 |
| `no_touch` | Board profile has no touch (default X4) |
| `no_home` | Board profile has no Home key (default X4) |

Key names (case-insensitive): `BACK`/`ESCAPE`, `ENTER`/`RETURN`/`CONFIRM`,
`LEFT`, `RIGHT`, `UP`, `DOWN`, `POWER`/`P`, plus `SLEEP`/`S` and `QUIT`
as host edges. Default key hold is 80 ms. Touch `kind`: 0 down, 1 move,
2 up, 3 tap. Swipe default duration is 250 ms. Touch and swipe
coordinates are panel pixels, clamped to the framebuffer.

### Observe

`InputObserved` is emitted for human SDL edges, remote injects, and
script edges. Host sleep/quit use host-edge observations. Observed
envelopes do **not** copy the host `corr` (they stay `0`) so they
cannot complete a host `send_and_wait`.

### Snapshots

`SnapshotRequest` is stored for the SDL main thread.
`SimulatorDisplay::presentIfNeeded` copies `pixelBuf` (optional region),
converts to 8-bit gray, and hands the buffer back. The worker encodes
PNG off that thread and sets `mime_type` `image/png` plus a SHA-256
hex `hash`. Format `0` is that PNG. A second request while one is
pending or ready returns `SnapshotError` `busy`. Encode failure is
`encode_failed`. Empty crops are `empty_region`.

### Logs

`HWCDC::write` and `HWCDC::printf` still print to stderr. After
`SimGrpc::start()`, complete serial lines are also teed as `LogLine`
(`LOG_TYPE_FIRMWARE_SERIAL`). Severity is taken from firmware tags
in the line (`[DBG]` / `[DEBUG]`, `[INF]` / `[INFO]`, `[WRN]` /
`[WARN]`, `[ERR]` / `[ERROR]`); untagged lines are `INFO`. The
component is the next `[tag]` after the severity (for example
`DREG`); otherwise `serial`. The `gStarted` gate exists because
static-init `Serial` must not construct protobuf messages before
`main`.

Host `[SIM]` diagnostics (storage open failures, screenshot capture,
frontlight, invalid instance id) still print to stderr and, after
`start()`, are teed as `LOG_TYPE_HOST_SIM` via `simHostLog`. Control
plane dial/retry stay `LOG_TYPE_CONTROL_PLANE`.

### Session view

`SetSessionView.read_mask` is a list of `SimToServer` payload names.
An empty mask emits everything. These payloads are **always** sent,
even when masked: `register`, `goodbye`, `input_ack`, `snapshot`,
`snapshot_error`. The mask may hide `heartbeat`, `log`, and
`input_observed`.

### Headless

`--sim-headless` / `CROSSPOINT_SIM_HEADLESS=1` creates the SDL window
with `SDL_WINDOW_HIDDEN` and drains keyboard, mouse, Home, sleep, and
`SDL_QUIT` without applying them. The event pump still runs. Remote
and script injects still apply. `Heartbeat.headless` is true. There
is no local quit path; the process exits on `ShutdownRequest` (or an
external signal).

### Reconnect and missing peers

If the host is down, the worker retries with backoff (200 ms,
doubling to 5 s) and logs `control plane: retrying` on stderr. A
missing host does **not** quit the SDL window. A missing simulator
does not take down the host. After `Goodbye` or a dropped stream, a
new dial is a new stream; the same `instance_id` is a returning
instance.

`ShutdownRequest` sets `quitRequested` and goodbye reason `shutdown`.

## Regenerating stubs

Do not run `protoc` or `buffa-build` in this repository. In
[crosspoint-simulator-mcp](https://github.com/canardleteer/crosspoint-simulator-mcp),
`cargo xtask generate-sim-cpp` runs Buf (via `buf-tools`) with
`protos/buf.gen.sim-cpp.yaml` and deposits C++ into this tree. Commit
the generated files here after that host-side generate.

## Related

- This repository's [`AGENTS.md`](../AGENTS.md#session-client)
  — compile gate, grpc++ only, deposited stubs, thread and queue
  rules, Linux-only testing, pre-`92520e1` bug commits
- Host MCP server and IDL:
  [canardleteer/crosspoint-simulator-mcp](https://github.com/canardleteer/crosspoint-simulator-mcp)
