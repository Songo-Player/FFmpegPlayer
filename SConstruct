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

# Android FFmpeg builds live in a separate subdirectory so they don't
# clobber the host (desktop) build for the same architecture.
if env["platform"] == "android":
    ffmpeg_dir = os.path.join(
        project_root,
        "build",
        "ffmpeg",
        "android",
        ffmpeg_arch,
    )
else:
    ffmpeg_dir = os.path.join(
        project_root,
        "build",
        "ffmpeg",
        ffmpeg_arch,
    )

ffmpeg_lib_dir = os.path.join(
    ffmpeg_dir,
    "lib",
)

ffmpeg_include_dir = os.path.join(
    ffmpeg_dir,
    "include",
)


# --------------------------------------------------
# Include paths
# --------------------------------------------------

env.Append(
    CPPPATH=[
        ffmpeg_include_dir,
        "src",
    ]
)


# --------------------------------------------------
# FFmpeg static libraries
# --------------------------------------------------

# Use SCons File nodes so these are treated as actual
# archive files rather than library names (-l...).
#
# Keeping these in LIBS causes SCons to place them after
# the extension's object files on the linker command line.

ffmpeg_static_libs = [
    env.File(os.path.join(ffmpeg_lib_dir, "libavformat.a")),
    env.File(os.path.join(ffmpeg_lib_dir, "libavcodec.a")),
    env.File(os.path.join(ffmpeg_lib_dir, "libswresample.a")),
    env.File(os.path.join(ffmpeg_lib_dir, "libavutil.a")),
]


# --------------------------------------------------
# Platform-specific linking
# --------------------------------------------------

if env["platform"] == "linux":

    env.Append(
        LINKFLAGS=[
            # FFmpeg's x86_64 CABAC code contains PC-relative
            # references to internal FFmpeg symbols. Treat symbols
            # defined by this shared library as non-preemptible.
            "-Wl,-Bsymbolic",
        ]
    )

    env.Append(
        LIBS=[
            *ffmpeg_static_libs,
            "pthread",
            "m",
            "dl",
        ]
    )


elif env["platform"] == "macos":

    env.Append(
        LIBS=[
            *ffmpeg_static_libs,
        ],
        LINKFLAGS=[
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
        LIBS=[
            *ffmpeg_static_libs,
            "bcrypt",
            "ws2_32",
            "secur32",
        ]
    )


elif env["platform"] == "android":

    env.Append(
        LINKFLAGS=[
            # Same rationale as the linux branch: keep this shared
            # library's own symbols non-preemptible.
            "-Wl,-Bsymbolic",
        ]
    )

    # Android's libc provides pthread; "log" is needed for FFmpeg's
    # Android logging hook, "android" for any NDK platform calls.
    env.Append(
        LIBS=[
            *ffmpeg_static_libs,
            "log",
            "android",
            "m",
            "dl",
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


# --------------------------------------------------
# Build shared library
# --------------------------------------------------

library = env.SharedLibrary(
    library_path,
    source=sources,
)

Default(library)
