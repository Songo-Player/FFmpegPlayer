#!/usr/bin/env python
import os

env = SConscript("godot-cpp/SConstruct")

# --------------------------------------------------
# Architecture
# --------------------------------------------------

arch = env.get("arch", "x86_64")

# Normalize architecture names used by different build tools.
if arch in ("x86_64", "x86-64", "amd64"):
    ffmpeg_arch = "x86_64"
elif arch in ("arm64", "aarch64"):
    ffmpeg_arch = "arm64"
else:
    raise RuntimeError(
        "Unsupported architecture for FFmpeg: {}".format(arch)
    )

print("Using FFmpeg build for architecture: {}".format(ffmpeg_arch))


# --------------------------------------------------
# FFmpeg
# --------------------------------------------------

project_root = os.getcwd()

ffmpeg_dir = os.path.join(
    project_root,
    "build",
    "ffmpeg",
    ffmpeg_arch,
)

ffmpeg_lib_dir = os.path.join(ffmpeg_dir, "lib")
ffmpeg_include_dir = os.path.join(ffmpeg_dir, "include")

env.Append(
    CPPPATH=[
        ffmpeg_include_dir,
        "src",
    ]
)

# Use the exact static archives.
ffmpeg_static_libs = [
    os.path.join(ffmpeg_lib_dir, "libavformat.a"),
    os.path.join(ffmpeg_lib_dir, "libavcodec.a"),
    os.path.join(ffmpeg_lib_dir, "libswresample.a"),
    os.path.join(ffmpeg_lib_dir, "libavutil.a"),
]


# --------------------------------------------------
# Platform-specific FFmpeg linking
# --------------------------------------------------

if env["platform"] == "linux":
    env.Append(
        LINKFLAGS=[
            "-Wl,--start-group",
            *ffmpeg_static_libs,
            "-Wl,--end-group",
        ]
    )

    env.Append(
        LIBS=[
            "pthread",
            "m",
            "dl",
        ]
    )

elif env["platform"] == "macos":
    env.Append(
        LINKFLAGS=ffmpeg_static_libs + [
            "-framework", "CoreFoundation",
            "-framework", "CoreVideo",
            "-framework", "CoreMedia",
            "-framework", "AudioToolbox",
            "-framework", "VideoToolbox",
            "-framework", "Security",
        ]
    )

elif env["platform"] == "windows":
    env.Append(
        LINKFLAGS=ffmpeg_static_libs
    )

    env.Append(
        LIBS=[
            "bcrypt",
            "ws2_32",
            "secur32",
        ]
    )


# --------------------------------------------------
# Extension sources
# --------------------------------------------------

sources = Glob("src/*.cpp")


# --------------------------------------------------
# Output
# --------------------------------------------------

if env["platform"] == "macos":
    library_path = (
        "bin/libffmpegplayer.{}.{}.framework/"
        "libffmpegplayer.{}.{}"
    ).format(
        env["platform"],
        env["target"],
        env["platform"],
        env["target"],
    )
else:
    library_path = "bin/libffmpegplayer{}{}".format(
        env["suffix"],
        env["SHLIBSUFFIX"],
    )


library = env.SharedLibrary(
    library_path,
    source=sources,
)

Default(library)