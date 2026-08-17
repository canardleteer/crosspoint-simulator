---
name: host-platforms
description: >-
  Use this skill when changing host-specific simulator build flags, MD5
  backends, SDL or OpenSSL linkage, or sample PlatformIO ini files, and when
  a task depends on macOS, Linux, WSL, or native Windows support. Covers
  CommonCrypto vs OpenSSL, sdl2-config, and the unsupported native Windows
  path.
license: MIT
---

# Host platforms

Load host-specific simulator guidance after detecting the machine that will
build or run the native binary. Keep always-on rules in the root
`AGENTS.md` file (HAL stubs, shared architecture, SDL on the main thread,
HiDPI hints). This skill holds only the host deltas.

## Detect the host

1. Read `uname -s`.
2. On Linux, also check for WSL (`WSL_DISTRO_NAME`, `/proc/sys/fs/binfmt_misc/WSLInterop`, or `Microsoft` in `/proc/version`).
3. On Linux, read `/etc/os-release` for `ID` and `VERSION_ID` when present.

Map the result to a candidate id:

| Detection | Candidate family |
| --------- | ---------------- |
| `Darwin` | `macos` |
| Linux, including WSL | `linux` |
| Native Windows, MinGW, or Cygwin | `windows` |

## Load references (most specific first)

Build a most-specific-first list, then **read only files that exist** under
`references/`. A more specific file is a delta on top of its parent, not a
copy.

Examples of future ids (no overlay files yet):

- `linux-ubuntu-24.04` → `linux-ubuntu` → `linux-wsl` → `linux`
- `macos-arm64` → `macos`

Today the only files are:

- [macOS](references/macos.md)
- [Linux](references/linux.md) (WSL uses this file)
- [Windows](references/windows.md)

If the host is WSL, still load `linux` until a `linux-wsl` overlay exists.
If the host is native Windows, load `windows` and stop; do not invent a
native Windows build path.

## When this skill does not apply

Device profiles (`FREEINK_DEVICE_*`, plus host-only `SIMULATOR_DISPLAY_*`),
input mapping, storage, and FreeRTOS shims are shared. Leave those in the
root `AGENTS.md` file.
