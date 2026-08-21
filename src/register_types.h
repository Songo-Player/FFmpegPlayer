#ifndef FFMPEG_REGISTER_TYPES_H
#define FFMPEG_REGISTER_TYPES_H

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void initialize_ffmpeg_module(ModuleInitializationLevel p_level);
void uninitialize_ffmpeg_module(ModuleInitializationLevel p_level);

#endif // FFMPEG_REGISTER_TYPES_H