#ifndef FFMPEG_PLAYER_H
#define FFMPEG_PLAYER_H

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/audio_stream_generator.hpp>
#include <godot_cpp/classes/audio_stream_generator_playback.hpp>
#include <godot_cpp/classes/audio_stream_player.hpp>

#include <atomic>
#include <mutex>
#include <string>
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

	// FFmpeg state, owned by the decode thread while it runs.
	AVFormatContext *format_ctx = nullptr;
	AVCodecContext *codec_ctx = nullptr;
	SwrContext *swr = nullptr;
	int audio_stream_index = -1;

	static void _decode_thread_func(FFMPEGPlayer *self, std::string path, double seek_time, int64_t session);
	static int _interrupt_callback(void *opaque);
	bool _open_input(const std::string &path, double seek_time, String &r_error);
	void _poll_stream_title();
	void _start_output(int64_t p_session, int p_mix_rate, double p_buffer_length);

	std::atomic<bool> playing = false;
	std::atomic<bool> paused = false;
	bool stream_finished = false;

	// Set by the decode thread once the input is open.
	std::atomic<bool> live = false;
	std::atomic<bool> seekable = false;
	// Set on the main thread once the generator playback exists.
	std::atomic<bool> output_ready = false;
	// Bumped on every play() so stale deferred calls can be ignored.
	std::atomic<int64_t> session_id = 0;

	mutable std::mutex mutex;
	uint64_t frames_played = 0;
	double position_offset = 0.0;
	String current_path;
	String stream_title;
	int64_t last_title_poll_us = 0;

	bool force_44100hz = false;
	double buffer_length = 0.2;
	double stream_buffer_length = 1.0;
	double network_timeout = 10.0;
	String user_agent;

public:
	void test_ffmpeg();

	void play(String path, double seek_time = 0.0);
	void stop();
	void pause();
	void resume();
	void seek(double seconds);

	bool is_paused() const;
	bool is_playing() const;
	bool is_live() const;
	bool is_seekable() const;
	double get_playback_position() const;
	String get_stream_title() const;

	void set_force_44100hz(bool p_force);
	bool get_force_44100hz() const;
	void set_buffer_length(double p_seconds);
	double get_buffer_length() const;
	void set_stream_buffer_length(double p_seconds);
	double get_stream_buffer_length() const;
	void set_network_timeout(double p_seconds);
	double get_network_timeout() const;
	void set_user_agent(const String &p_user_agent);
	String get_user_agent() const;

	Ref<AudioStreamGenerator> get_generator() const { return generator; }
	AudioStreamPlayer *get_player() const { return player; }

	FFMPEGPlayer();
	~FFMPEGPlayer();
};

} // namespace godot

#endif // FFMPEG_PLAYER_H
