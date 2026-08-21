#!/usr/bin/env python
import os

env = SConscript("godot-cpp/SConstruct")

# --- ffmpeg static libs ---
ffmpeg_dir = os.path.join(os.getcwd(), "build", "ffmpeg")
env.Append(CPPPATH=[os.path.join(ffmpeg_dir, "include")])
env.Append(LIBPATH=[os.path.join(ffmpeg_dir, "lib")])

# Order matters for static linking: avformat depends on avcodec,
# both depend on swresample/avutil. avutil goes last.
ffmpeg_libs = ["avformat", "avcodec", "swresample", "avutil"]
env.Append(LIBS=ffmpeg_libs)

if env["platform"] == "linux":
    env.Append(LIBS=["pthread", "m", "dl"])
elif env["platform"] == "macos":
    env.Append(
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
    env.Append(LIBS=["bcrypt", "ws2_32", "secur32"])

# --- extension sources ---
env.Append(CPPPATH=["src/"])
sources = Glob("src/*.cpp")

if env["platform"] == "macos":
    library_path = "demo/bin/libffmpegplayer.{}.{}.framework/libffmpegplayer.{}.{}".format(
        env["platform"], env["target"], env["platform"], env["target"]
    )
else:
    library_path = "demo/bin/libffmpegplayer{}{}".format(env["suffix"], env["SHLIBSUFFIX"])

library = env.SharedLibrary(
    library_path,
    source=sources,
)

Default(library)