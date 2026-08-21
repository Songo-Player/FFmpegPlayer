#!/usr/bin/env python
import os

env = SConscript("godot-cpp/SConstruct")

# --------------------------------------------------
# FFmpeg
# --------------------------------------------------

ffmpeg_dir = os.path.join(os.getcwd(), "build", "ffmpeg")
ffmpeg_lib_dir = os.path.join(ffmpeg_dir, "lib")
ffmpeg_include_dir = os.path.join(ffmpeg_dir, "include")

env.Append(
    CPPPATH=[
        ffmpeg_include_dir,
        "src",
    ]
)

# Use the exact static archives rather than relying on -lavcodec etc.
ffmpeg_static_libs = [
    os.path.join(ffmpeg_lib_dir, "libavformat.a"),
    os.path.join(ffmpeg_lib_dir, "libavcodec.a"),
    os.path.join(ffmpeg_lib_dir, "libswresample.a"),
    os.path.join(ffmpeg_lib_dir, "libavutil.a"),
]

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

if env["platform"] == "macos":
    library_path = (
        "demo/bin/libffmpegplayer.{}.{}.framework/"
        "libffmpegplayer.{}.{}"
    ).format(
        env["platform"],
        env["target"],
        env["platform"],
        env["target"],
    )
else:
    library_path = "demo/bin/libffmpegplayer{}{}".format(
        env["suffix"],
        env["SHLIBSUFFIX"],
    )

library = env.SharedLibrary(
    library_path,
    source=sources,
)

Default(library)