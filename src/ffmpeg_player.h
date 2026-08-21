#ifndef FFMPEG_PLAYER_H
#define FFMPEG_PLAYER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

namespace godot {

class FFMPEGPlayer : public Node {
	GDCLASS(FFMPEGPlayer, Node)

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	Ref<AudioStreamGenerator> generator;
	Ref<AudioStreamGeneratorPlayback> playback;
	AudioStreamPlayer *player = nullptr;

	std::thread decode_thread;

	// FFmpeg state
	AVFormatContext *format_ctx = nullptr;
	AVCodecContext *codec_ctx = nullptr;
	SwrContext *swr = nullptr;
	int audio_stream_index = -1;

	static void _decode_thread_func(FFMPEGPlayer *self);

	std::atomic<bool> playing = false;
	std::atomic<bool> paused = false;
	bool stream_finished = false;

	mutable std::mutex mutex;
	uint64_t frames_played = 0;
	String current_path;

public:
	void test_ffmpeg();

	void play(String path, double seek_time = 0.0);
	void stop();
	void pause();
	void resume();
	void seek(double seconds);

	bool is_paused() const;
	bool is_playing() const;
	double get_playback_position() const;

	Ref<AudioStreamGenerator> get_generator() const { return generator; }
	AudioStreamPlayer *get_player() const { return player; }

	FFMPEGPlayer();
	~FFMPEGPlayer();
};

} // namespace godot

#endif // FFMPEG_PLAYER_H