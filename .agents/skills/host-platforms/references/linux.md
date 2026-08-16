# Linux

WSL follows this file. Native Windows does not; see
[windows.md](windows.md).

## MD5

[MD5Builder.h](../../../../src/MD5Builder.h) selects
[MD5Builder_linux.h](../../../../src/MD5Builder_linux.h) under `__linux__`.
That path uses OpenSSL. Downstream firmware only needs to include
`MD5Builder.h`.

## Sample PlatformIO flags

Use [sample-platformio-linux-wsl.ini](../../../../sample-platformio-linux-wsl.ini).
Linux/WSL additionally links OpenSSL with `-lssl -lcrypto
-Wno-deprecated-declarations` (OpenSSL 3.x deprecates `MD5_*`). Keep this
file in sync with
[sample-platformio-macos.ini](../../../../sample-platformio-macos.ini)
when build flags change.
