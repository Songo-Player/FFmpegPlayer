#include "ffmpeg_player.h"

#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

#include <algorithm>

using namespace godot;

namespace {

String _av_error_string(int p_err) {
	char errbuf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(p_err, errbuf, sizeof(errbuf));
	return String(errbuf);
}

bool _is_valid_utf8(const std::string &p_str) {
	size_t i = 0;
	while (i < p_str.size()) {
		unsigned char c = (unsigned char)p_str[i];
		int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
		if (extra < 0 || i + extra >= p_str.size()) {
			return false;
		}
		for (int k = 1; k <= extra; k++) {
			if ((((unsigned char)p_str[i + k]) >> 6) != 0x2) {
				return false;
			}
		}
		i += extra + 1;
	}
	return true;
}

// ICY titles are usually UTF-8 but plenty of older servers send Latin-1.
String _decode_icy_string(const std::string &p_str) {
	if (_is_valid_utf8(p_str)) {
		return String::utf8(p_str.c_str(), (int)p_str.size());
	}
	String out;
	for (unsigned char c : p_str) {
		out += (char32_t)c;
	}
	return out;
}

} // namespace

void FFMPEGPlayer::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));
	ADD_SIGNAL(MethodInfo("error", PropertyInfo(Variant::STRING, "message")));
	ADD_SIGNAL(MethodInfo("stream_title_changed", PropertyInfo(Variant::STRING, "title")));

	ClassDB::bind_method(D_METHOD("test_ffmpeg"), &FFMPEGPlayer::test_ffmpeg);
	ClassDB::bind_method(D_METHOD("play", "path", "seek_time"), &FFMPEGPlayer::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop"), &FFMPEGPlayer::stop);
	ClassDB::bind_method(D_METHOD("pause"), &FFMPEGPlayer::pause);
	ClassDB::bind_method(D_METHOD("resume"), &FFMPEGPlayer::resume);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &FFMPEGPlayer::seek);
	ClassDB::bind_method(D_METHOD("is_paused"), &FFMPEGPlayer::is_paused);
	ClassDB::bind_method(D_METHOD("is_playing"), &FFMPEGPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("is_live"), &FFMPEGPlayer::is_live);
	ClassDB::bind_method(D_METHOD("is_seekable"), &FFMPEGPlayer::is_seekable);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &FFMPEGPlayer::get_playback_position);
	ClassDB::bind_method(D_METHOD("get_stream_title"), &FFMPEGPlayer::get_stream_title);
	ClassDB::bind_method(D_METHOD("get_generator"), &FFMPEGPlayer::get_generator);
	ClassDB::bind_method(D_METHOD("get_player"), &FFMPEGPlayer::get_player);
	ClassDB::bind_method(D_METHOD("set_force_44100hz", "force"), &FFMPEGPlayer::set_force_44100hz);
	ClassDB::bind_method(D_METHOD("get_force_44100hz"), &FFMPEGPlayer::get_force_44100hz);
	ClassDB::bind_method(D_METHOD("set_buffer_length", "seconds"), &FFMPEGPlayer::set_buffer_length);
	ClassDB::bind_method(D_METHOD("get_buffer_length"), &FFMPEGPlayer::get_buffer_length);
	ClassDB::bind_method(D_METHOD("set_stream_buffer_length", "seconds"), &FFMPEGPlayer::set_stream_buffer_length);
	ClassDB::bind_method(D_METHOD("get_stream_buffer_length"), &FFMPEGPlayer::get_stream_buffer_length);
	ClassDB::bind_method(D_METHOD("set_network_timeout", "seconds"), &FFMPEGPlayer::set_network_timeout);
	ClassDB::bind_method(D_METHOD("get_network_timeout"), &FFMPEGPlayer::get_network_timeout);
	ClassDB::bind_method(D_METHOD("set_user_agent", "user_agent"), &FFMPEGPlayer::set_user_agent);
	ClassDB::bind_method(D_METHOD("get_user_agent"), &FFMPEGPlayer::get_user_agent);
	ClassDB::bind_method(D_METHOD("_start_output", "session", "mix_rate", "buffer_length"), &FFMPEGPlayer::_start_output);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "generator", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamGenerator"), "", "get_generator");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamPlayer"), "", "get_player");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "force_44100hz"), "set_force_44100hz", "get_force_44100hz");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "buffer_length", PROPERTY_HINT_RANGE, "0.05,10,0.01,suffix:s"), "set_buffer_length", "get_buffer_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "stream_buffer_length", PROPERTY_HINT_RANGE, "0.05,10,0.01,suffix:s"), "set_stream_buffer_length", "get_stream_buffer_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "network_timeout", PROPERTY_HINT_RANGE, "1,120,0.5,suffix:s"), "set_network_timeout", "get_network_timeout");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "user_agent"), "set_user_agent", "get_user_agent");
}

void FFMPEGPlayer::test_ffmpeg() {
	unsigned version = avcodec_version();

	if (version == 0) {
		UtilityFunctions::print("FFmpeg failed to load.");
		return;
	}

	UtilityFunctions::print("FFmpeg loaded successfully!");
	UtilityFunctions::print("avcodec version: ", version);
}

FFMPEGPlayer::FFMPEGPlayer() {
	generator.instantiate();
	generator->set_mix_rate(44100);
	generator->set_buffer_length(buffer_length);

	player = memnew(AudioStreamPlayer);
	add_child(player);
	player->set_stream(generator);
}

FFMPEGPlayer::~FFMPEGPlayer() {
	stop();
}

void FFMPEGPlayer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PREDELETE: {
			// Ref<> members clean themselves up automatically.
		} break;
	}
}

int FFMPEGPlayer::_interrupt_callback(void *opaque) {
	// Lets stop() abort blocking network calls (connect, read, reconnect waits).
	FFMPEGPlayer *self = static_cast<FFMPEGPlayer *>(opaque);
	return self->playing ? 0 : 1;
}

void FFMPEGPlayer::play(String path, double seek_time) {
	// Always tear down the previous session, including one whose decode
	// thread already finished on its own and is still joinable.
	stop();

	current_path = path;
	{
		std::lock_guard<std::mutex> lock(mutex);
		frames_played = 0;
		position_offset = seek_time > 0.0 ? seek_time : 0.0;
		stream_title = String();
	}
	playing = true;

	// Opening a network stream (DNS, TLS, probing) can take seconds, so it
	// happens on the decode thread rather than here.
	int64_t session = ++session_id;
	decode_thread = std::thread(_decode_thread_func, this, std::string(path.utf8().get_data()), seek_time, session);
}

bool FFMPEGPlayer::_open_input(const std::string &path, double seek_time, String &r_error) {
	format_ctx = avformat_alloc_context();
	if (!format_ctx) {
		r_error = "Failed to allocate format context";
		return false;
	}
	format_ctx->interrupt_callback.callback = _interrupt_callback;
	format_ctx->interrupt_callback.opaque = this;

	// Protocol options; ones that don't apply to the input (e.g. http options
	// for a local file) are simply left unused.
	AVDictionary *options = nullptr;
	av_dict_set_int(&options, "rw_timeout", (int64_t)(network_timeout * 1000000.0), 0);
	av_dict_set(&options, "reconnect", "1", 0);
	av_dict_set(&options, "reconnect_streamed", "1", 0);
	av_dict_set(&options, "reconnect_on_network_error", "1", 0);
	av_dict_set(&options, "reconnect_delay_max", "5", 0);
	av_dict_set(&options, "icy", "1", 0);
	if (!user_agent.is_empty()) {
		av_dict_set(&options, "user_agent", user_agent.utf8().get_data(), 0);
	}

	int ret = avformat_open_input(&format_ctx, path.c_str(), nullptr, &options);
	av_dict_free(&options);
	if (ret < 0) {
		// avformat_open_input frees the context on failure.
		format_ctx = nullptr;
		r_error = "Failed to open " + String::utf8(path.c_str()) + ": " + _av_error_string(ret);
		return false;
	}

	ret = avformat_find_stream_info(format_ctx, nullptr);
	if (ret < 0) {
		r_error = "Failed to read stream info: " + _av_error_string(ret);
		return false;
	}

	const AVCodec *codec = nullptr;
	audio_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
	if (audio_stream_index < 0 || !codec) {
		r_error = "No playable audio stream found";
		audio_stream_index = -1;
		return false;
	}

	// HLS playlists can expose several variants/renditions; discarding the
	// unused ones stops FFmpeg from downloading their segments too.
	for (unsigned i = 0; i < format_ctx->nb_streams; i++) {
		if ((int)i != audio_stream_index) {
			format_ctx->streams[i]->discard = AVDISCARD_ALL;
		}
	}

	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		r_error = "Failed to allocate codec context";
		return false;
	}
	avcodec_parameters_to_context(codec_ctx, format_ctx->streams[audio_stream_index]->codecpar);
	ret = avcodec_open2(codec_ctx, codec, nullptr);
	if (ret < 0) {
		r_error = "Failed to open decoder: " + _av_error_string(ret);
		return false;
	}

	// Live radio (Icecast/Shoutcast, live HLS) has no known duration.
	live = format_ctx->duration == AV_NOPTS_VALUE;
	seekable = !live;

	// Resample to stereo float, either at the source's native rate or
	// forced to 44100Hz, matching whatever the generator is set to.
	int target_rate = force_44100hz ? 44100 : codec_ctx->sample_rate;

	AVChannelLayout out_layout;
	av_channel_layout_default(&out_layout, 2);
	ret = swr_alloc_set_opts2(
			&swr,
			&out_layout, AV_SAMPLE_FMT_FLT, target_rate,
			&codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
			0, nullptr);
	if (ret < 0 || swr_init(swr) < 0) {
		r_error = "Failed to initialize resampler";
		return false;
	}

	if (seek_time > 0.0 && seekable) {
		int64_t ts = (int64_t)(seek_time * AV_TIME_BASE);
		av_seek_frame(format_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(codec_ctx);
	}

	return true;
}

void FFMPEGPlayer::_start_output(int64_t p_session, int p_mix_rate, double p_buffer_length) {
	// Runs on the main thread; AudioStreamPlayer is not safe to drive from
	// the decode thread.
	if (p_session != session_id || !playing) {
		return;
	}

	generator->set_mix_rate((double)p_mix_rate);
	generator->set_buffer_length(p_buffer_length);
	player->play();

	Ref<AudioStreamPlayback> pb = player->get_stream_playback();
	playback = Ref<AudioStreamGeneratorPlayback>(Object::cast_to<AudioStreamGeneratorPlayback>(pb.ptr()));
	output_ready = true;
}

void FFMPEGPlayer::_poll_stream_title() {
	if (!format_ctx->pb) {
		return;
	}

	int64_t now = av_gettime_relative();
	if (now - last_title_poll_us < 500000) {
		return;
	}
	last_title_poll_us = now;

	uint8_t *packet = nullptr;
	if (av_opt_get(format_ctx->pb, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN, &packet) < 0 || !packet) {
		return;
	}
	std::string meta((const char *)packet);
	av_free(packet);

	// Format: StreamTitle='Artist - Title';StreamUrl='...';
	static const std::string key = "StreamTitle='";
	size_t start = meta.find(key);
	if (start == std::string::npos) {
		return;
	}
	start += key.size();
	size_t end = meta.find("';", start);
	if (end == std::string::npos) {
		end = meta.rfind('\'');
		if (end == std::string::npos || end < start) {
			end = meta.size();
		}
	}

	String title = _decode_icy_string(meta.substr(start, end - start)).strip_edges();
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (title == stream_title) {
			return;
		}
		stream_title = title;
	}
	call_deferred("emit_signal", "stream_title_changed", title);
}

void FFMPEGPlayer::_decode_thread_func(FFMPEGPlayer *self, std::string path, double seek_time, int64_t session) {
	if (!self) {
		return;
	}

	String open_error;
	if (!self->_open_input(path, seek_time, open_error)) {
		if (self->playing) {
			UtilityFunctions::print(open_error);
			self->call_deferred("emit_signal", "error", open_error);
		}
		self->playing = false;
		return;
	}

	int target_rate = self->force_44100hz ? 44100 : self->codec_ctx->sample_rate;
	double buffer_seconds = self->live ? self->stream_buffer_length : self->buffer_length;
	self->call_deferred("_start_output", session, target_rate, buffer_seconds);

	while (self->playing && !self->output_ready) {
		OS::get_singleton()->delay_usec(1000);
	}

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	std::vector<float> pcm_buffer;
	pcm_buffer.resize(2048 * 2);
	PackedVector2Array push_buffer;

	String fatal_error;
	int consecutive_errors = 0;

	while (self->playing) {
		if (self->paused) {
			OS::get_singleton()->delay_usec(5000);
			continue;
		}

		int read_ret = av_read_frame(self->format_ctx, packet);

		if (read_ret < 0) {
			if (!self->playing || read_ret == AVERROR_EXIT) {
				break;
			}
			if (read_ret == AVERROR_EOF) {
				break;
			}
			if (read_ret == AVERROR(EAGAIN)) {
				OS::get_singleton()->delay_usec(10000);
				continue;
			}
			// FFmpeg already retried the connection per the reconnect
			// options, so repeated failures mean the stream is gone.
			if (++consecutive_errors >= 10) {
				fatal_error = "Stream read failed: " + _av_error_string(read_ret);
				break;
			}
			OS::get_singleton()->delay_usec(100000);
			continue;
		}
		consecutive_errors = 0;

		if (self->live) {
			self->_poll_stream_title();
		}

		if (packet->stream_index != self->audio_stream_index) {
			av_packet_unref(packet);
			continue;
		}

		avcodec_send_packet(self->codec_ctx, packet);

		while (self->playing && avcodec_receive_frame(self->codec_ctx, frame) == 0) {
			int max_out = swr_get_out_samples(self->swr, frame->nb_samples);
			size_t needed = (size_t)max_out * 2; // *2 for stereo
			if (pcm_buffer.size() < needed) {
				pcm_buffer.resize(needed);
			}

			float *pcm_ptr = pcm_buffer.data();

			int out_samples = swr_convert(
					self->swr,
					(uint8_t **)&pcm_ptr,
					max_out,
					(const uint8_t **)frame->extended_data,
					frame->nb_samples);

			int written = 0;

			while (written < out_samples && self->playing) {
				if (!self->playback.is_valid()) {
					break;
				}

				int available = self->playback->get_frames_available();
				if (available <= 0 || self->paused) {
					OS::get_singleton()->delay_usec(1000);
					continue;
				}

				int count = std::min(available, out_samples - written);
				push_buffer.resize(count);
				Vector2 *dst = push_buffer.ptrw();
				const float *src = pcm_buffer.data() + (size_t)written * 2;
				for (int i = 0; i < count; i++) {
					dst[i] = Vector2(src[i * 2], src[i * 2 + 1]);
				}
				self->playback->push_buffer(push_buffer);

				{
					std::lock_guard<std::mutex> lock(self->mutex);
					self->frames_played += count;
				}
				written += count;
			}
		}

		av_packet_unref(packet);
	}

	av_frame_free(&frame);
	av_packet_free(&packet);

	if (self->playing) {
		if (!fatal_error.is_empty()) {
			UtilityFunctions::print(fatal_error);
			self->call_deferred("emit_signal", "error", fatal_error);
		} else {
			self->call_deferred("emit_signal", "finished");
		}
		self->stream_finished = true;
	}
	self->playing = false;
}

double FFMPEGPlayer::get_playback_position() const {
	std::lock_guard<std::mutex> lock(mutex);

	if (!generator.is_valid()) {
		return 0.0;
	}

	return position_offset + (double)frames_played / generator->get_mix_rate();
}

String FFMPEGPlayer::get_stream_title() const {
	std::lock_guard<std::mutex> lock(mutex);
	return stream_title;
}

void FFMPEGPlayer::stop() {
	playing = false;
	paused = false;

	if (decode_thread.joinable()) {
		decode_thread.join();
	}

	stream_finished = false;
	output_ready = false;
	live = false;
	seekable = false;
	last_title_poll_us = 0;

	if (player) {
		player->stop();
	}

	if (playback.is_valid()) {
		playback->clear_buffer();
	}
	playback = Ref<AudioStreamGeneratorPlayback>();

	if (codec_ctx) {
		avcodec_free_context(&codec_ctx);
		codec_ctx = nullptr;
	}

	if (format_ctx) {
		avformat_close_input(&format_ctx);
		format_ctx = nullptr;
	}

	if (swr) {
		swr_free(&swr);
		swr = nullptr;
	}

	audio_stream_index = -1;
}

void FFMPEGPlayer::pause() {
	paused = true;

	if (player) {
		player->set_stream_paused(true);
	}

	if (playback.is_valid()) {
		playback->clear_buffer();
	}
}

void FFMPEGPlayer::resume() {
	if (paused && live) {
		// The server kept broadcasting (or dropped us) while we were paused,
		// so rejoin the live stream rather than play stale buffered data.
		play(current_path);
		return;
	}

	paused = false;

	if (player) {
		player->set_stream_paused(false);
	}
}

bool FFMPEGPlayer::is_paused() const {
	return paused;
}

bool FFMPEGPlayer::is_playing() const {
	return !paused && playing;
}

bool FFMPEGPlayer::is_live() const {
	return live;
}

bool FFMPEGPlayer::is_seekable() const {
	return seekable;
}

void FFMPEGPlayer::set_force_44100hz(bool p_force) {
	force_44100hz = p_force;
}

bool FFMPEGPlayer::get_force_44100hz() const {
	return force_44100hz;
}

void FFMPEGPlayer::set_buffer_length(double p_seconds) {
	buffer_length = p_seconds;
}

double FFMPEGPlayer::get_buffer_length() const {
	return buffer_length;
}

void FFMPEGPlayer::set_stream_buffer_length(double p_seconds) {
	stream_buffer_length = p_seconds;
}

double FFMPEGPlayer::get_stream_buffer_length() const {
	return stream_buffer_length;
}

void FFMPEGPlayer::set_network_timeout(double p_seconds) {
	network_timeout = p_seconds;
}

double FFMPEGPlayer::get_network_timeout() const {
	return network_timeout;
}

void FFMPEGPlayer::set_user_agent(const String &p_user_agent) {
	user_agent = p_user_agent;
}

String FFMPEGPlayer::get_user_agent() const {
	return user_agent;
}

void FFMPEGPlayer::seek(double seconds) {
	if (!seekable) {
		return;
	}

	play(current_path, seconds);
}
