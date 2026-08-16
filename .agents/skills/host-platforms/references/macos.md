# macOS

## MD5

[MD5Builder.h](../../../../src/MD5Builder.h) selects
[MD5Builder_mac.h](../../../../src/MD5Builder_mac.h) under `__APPLE__`.
That path uses CommonCrypto. Downstream firmware only needs to include
`MD5Builder.h`.

## Sample PlatformIO flags

Use [sample-platformio-macos.ini](../../../../sample-platformio-macos.ini).
macOS gets architecture-correct SDL compiler and linker flags from
`sdl2-config`, so the same sample works on Intel and Apple Silicon. Keep
this file in sync with
[sample-platformio-linux-wsl.ini](../../../../sample-platformio-linux-wsl.ini)
when build flags change.

## Why the shared SDL rules exist

macOS requires all SDL calls to come from the main thread. That is why
`refreshDisplay` stays off the SDL path and `presentIfNeeded` runs on the
main thread.

Without `SDL_HINT_RENDER_SCALE_QUALITY=1` (set before
`SDL_CreateTexture`), `SDL_WINDOW_ALLOW_HIGHDPI`, and
`SDL_RenderSetLogicalSize`, Bayer-dithered grays render as harsh
black/white stripes on Retina.
