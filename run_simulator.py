"""
PlatformIO library build script for the Crosspoint Simulator.

Handles two things automatically when this lib is included as a lib_dep:

1. Patches BookMetadataCache -- SpineEntry::cumulativeSize and its fast read
   path can use size_t, which is 8 bytes on 64-bit hosts (macOS/Linux) but
   4 bytes on ESP32-C3. This mismatch breaks binary cache serialization in the
   simulator. Replaced with uint32_t, which is the correct explicit size on both
   platforms. Applied idempotently -- safe to run on every build.

2. Patches GfxRenderer::setOrientation so simulator builds notify HalDisplay
   when the logical orientation changes. Without this, the framebuffer content
   can rotate while the SDL window keeps its startup portrait/landscape shape.

3. Registers a backward-compatible "run_simulator" custom target.

This file can be loaded more than once in the same PlatformIO process:
- once from this library's `library.json` build hook
- again indirectly when a consuming firmware repo adds the separate
  `run_simulator_project.py` helper for IDE task exposure

Use a process-wide sentinel so the custom target is registered only once even
when multiple registration paths exist.
"""

Import("env")
try:
    Import("pio_lib_builder")
except Exception:
    pio_lib_builder = None
import os
import builtins
import re

try:
    _LIB_DIR = os.path.dirname(os.path.abspath(__file__))
except NameError:
    _LIB_DIR = os.getcwd()

RUN_SIMULATOR_TARGET_KEY = "_crosspoint_run_simulator_target_registered"
RUN_SIMULATOR_TARGET_OWNER_OPTION = "custom_run_simulator_target_owner"
SIMULATOR_HTTP_PORT_OPTION = "custom_simulator_http_port"


# --- run_simulator custom target ---

def _run_simulator(source, target, env):
    import subprocess

    binary = env.subst("$BUILD_DIR/program")
    runtime_env = os.environ.copy()
    configured_http_port = env.GetProjectOption(
        SIMULATOR_HTTP_PORT_OPTION, ""
    ).strip()
    if configured_http_port:
        runtime_env["CROSSPOINT_SIM_HTTP_PORT"] = configured_http_port
    subprocess.run([binary], cwd=os.getcwd(), env=runtime_env)


SIMULATOR_DIR = (
    pio_lib_builder.path
    if pio_lib_builder is not None
    else os.getcwd()
)
COMPAT_HAL_DIR = os.path.join(SIMULATOR_DIR, "src", "compat_hal")
SIMULATOR_SRC_DIR = os.path.join(SIMULATOR_DIR, "src")
THIN_FIRMWARE_HAL_HEADERS = (
    "HalDisplay.h",
    "HalFrontlight.h",
    "HalTiltSensor.h",
    "HalClock.h",
)
THIN_FIRMWARE_HAL_SOURCES = (
    "HalDisplay.cpp",
    "HalFrontlight.cpp",
    "HalTiltSensor.cpp",
    "HalClock.cpp",
)


def _firmware_hal_dir(build_env):
    return os.path.join(build_env.subst("$PROJECT_DIR"), "lib", "hal")


def _has_firmware_thin_hal(firmware_hal, headers=THIN_FIRMWARE_HAL_HEADERS):
    return all(
        os.path.isfile(os.path.join(firmware_hal, name)) for name in headers
    )


def _consumer_hal_overlay(
    firmware_hal, build_dir, headers=THIN_FIRMWARE_HAL_HEADERS
):
    overlay = os.path.join(build_dir, "consumer_hal_inc")
    os.makedirs(overlay, exist_ok=True)
    for name in headers:
        src = os.path.abspath(os.path.join(firmware_hal, name))
        dst = os.path.join(overlay, name)
        if os.path.lexists(dst):
            os.remove(dst)
        os.symlink(src, dst)
    return overlay


def _configure_consumer_hal(
    build_env,
    compat_hal_dir=COMPAT_HAL_DIR,
    simulator_src_dir=os.path.join(SIMULATOR_DIR, "src"),
    thin_sources=THIN_FIRMWARE_HAL_SOURCES,
):
    firmware_hal = _firmware_hal_dir(build_env)
    if not _has_firmware_thin_hal(firmware_hal):
        build_env.Append(CPPPATH=[compat_hal_dir])
        return

    # Keep lib_ignore = hal so firmware HalStorage.h / HalGPIO.h never enter
    # the include path. Compile the thin firmware TUs as project sources and
    # expose only those four headers.
    build_env.Replace(SRC_FILTER=["+<*>", "-<compat_hal/>"])
    from SCons.Script import DefaultEnvironment

    project_env = DefaultEnvironment()
    overlay = _consumer_hal_overlay(
        firmware_hal, project_env.subst("$BUILD_DIR")
    )
    include_dirs = [overlay, simulator_src_dir]
    logging_dir = os.path.join(project_env.subst("$PROJECT_DIR"), "lib", "Logging")
    if os.path.isdir(logging_dir):
        include_dirs.append(logging_dir)
    project_env.Prepend(CPPPATH=include_dirs)
    build_env.Prepend(CPPPATH=include_dirs)
    sources = [
        name
        for name in thin_sources
        if os.path.isfile(os.path.join(firmware_hal, name))
    ]
    if sources:
        hal_env = project_env.Clone()
        hal_env.Prepend(CPPPATH=include_dirs)
        hal_env.Append(CCFLAGS=["-I%s" % path for path in include_dirs])
        hal_env.BuildSources(
            os.path.join(project_env.subst("$BUILD_DIR"), "consumer_hal"),
            firmware_hal,
            src_filter=" ".join("+<%s>" % name for name in sources),
        )


_configure_consumer_hal(env)

target_owner = env.GetProjectOption(RUN_SIMULATOR_TARGET_OWNER_OPTION, "").strip().lower()

if target_owner != "project" and not getattr(builtins, RUN_SIMULATOR_TARGET_KEY, False):
    setattr(builtins, RUN_SIMULATOR_TARGET_KEY, True)
    env.AddCustomTarget(
        name="run_simulator",
        dependencies="$PROGPATH",
        actions=_run_simulator,
        title="Run Simulator",
        description="Build and run the desktop simulator",
        always_build=True,
    )


def _flag_list(value):
    if value is None:
        return []
    if isinstance(value, str):
        return [value]
    return list(value)


def _has_cpp_define(name):
    for item in env.get("CPPDEFINES", []):
        key = item[0] if isinstance(item, (list, tuple)) else item
        if str(key) == name:
            return True
    for flag_key in ("CCFLAGS", "CXXFLAGS", "CPPFLAGS"):
        for flag in env.get(flag_key, []):
            text = str(flag)
            if text == f"-D{name}" or text.startswith(f"-D{name}="):
                return True
    sources = []
    try:
        sources.extend(_flag_list(env.GetProjectOption("build_flags")))
    except Exception:
        pass
    try:
        from platformio.project.config import ProjectConfig

        pioenv = env.get("PIOENV")
        if pioenv:
            sources.extend(
                _flag_list(
                    ProjectConfig.get_instance().get(
                        f"env:{pioenv}", "build_flags"
                    )
                )
            )
    except Exception:
        pass
    return any(name in str(flag) for flag in sources)


def _parse_grpc_pkgconfig(target_env):
    try:
        target_env.ParseConfig("pkg-config --cflags --libs grpc++ protobuf")
    except Exception as exc:
        print(
            "[SIM] CROSSPOINT_SIM_GRPC is set but pkg-config grpc++ / protobuf "
            "failed. Point PKG_CONFIG_PATH at a local protobuf 35 / grpc++ 1.83 "
            "prefix (Ubuntu apt 3.21 / 1.51 is too old for the deposited stubs)."
        )
        raise exc
    # Static prefixes keep upb / c-ares / re2 / address_sorting in
    # Libs.private. pkg-config --static can hang on circular Requires.
    try:
        import subprocess

        libdir = subprocess.check_output(
            ["pkg-config", "--variable=libdir", "grpc++"],
            text=True,
            timeout=5,
        ).strip()
    except Exception:
        return
    extra = []
    pc = os.path.join(libdir, "pkgconfig", "grpc++.pc")
    if os.path.isfile(pc):
        for line in open(pc, encoding="utf-8"):
            if line.startswith("Libs.private:"):
                extra.extend(
                    flag[2:]
                    for flag in line.split(":", 1)[1].split()
                    if flag.startswith("-l")
                )
    for name in ("cares", "re2"):
        if os.path.isfile(os.path.join(libdir, f"lib{name}.a")) or os.path.isfile(
            os.path.join(libdir, f"lib{name}.so")
        ):
            extra.append(name)
    if extra:
        target_env.Append(LIBS=extra)
        target_env.Append(LIBPATH=[libdir])
    if libdir:
        target_env.Append(LINKFLAGS=[f"-Wl,-rpath,{libdir}"])


def _enable_session_client():
    print("[SIM] compiling Session client (CROSSPOINT_SIM_GRPC)")
    env.Append(CPPPATH=[os.path.join(_LIB_DIR, "src", "sim_grpc", "gen")])
    _parse_grpc_pkgconfig(env)
    env.BuildSources(
        os.path.join("$BUILD_DIR", "sim_grpc"),
        os.path.join(_LIB_DIR, "src", "sim_grpc", "gen"),
    )
    try:
        Import("projenv")
        _parse_grpc_pkgconfig(projenv)
    except Exception:
        try:
            from SCons.Script import DefaultEnvironment

            _parse_grpc_pkgconfig(DefaultEnvironment())
        except Exception:
            pass


if _has_cpp_define("CROSSPOINT_SIM_GRPC"):
    _enable_session_client()
