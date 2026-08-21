#include "ffmpeg_player.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void FFMPEGPlayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("test_ffmpeg"), &FFMPEGPlayer::test_ffmpeg);
}

FFMPEGPlayer::FFMPEGPlayer() {}

FFMPEGPlayer::~FFMPEGPlayer() {}

void FFMPEGPlayer::test_ffmpeg() {
	unsigned version = avcodec_version();
	if (version == 0) {
		UtilityFunctions::print("FFmpeg failed to load.");
		return;
	}
	UtilityFunctions::print("FFmpeg loaded successfully!");
	UtilityFunctions::print("avcodec version: ", version);
}