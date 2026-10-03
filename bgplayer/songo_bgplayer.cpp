// Songo background player: keeps the queue playing after Songo exits.
//
// Plays an m3u written by Songo through ALSA, looping the queue the way Songo
// does, until Select is held for a second or it gets SIGTERM/SIGINT.
// Select+Right skips to the next song, Select+Left restarts the current one,
// or goes back a song within its first 3 seconds. Select+Up/Down changes the
// music volume, so it can be balanced against whatever else is playing.
//
//   songo_bgplayer <playlist.m3u> [--blend SECONDS]
//
// --blend crossfades into the next song over that many seconds, like Songo's
// playback blend. 0 (the default) plays songs back to back.
//
// Besides the paths, the playlist can carry Songo's playback state as comment
// lines, which other m3u readers ignore:
//   #SONGO-START-INDEX:3      queue index to start at
//   #SONGO-START-POSITION:42  seconds into that song
//   #SONGO-REPEAT-ONE:1       repeat the current song instead of moving on
//   #SONGO-VOLUME:0.8         linear gain, Songo's music volume
//   #SONGO-EQ:0,-3,2,...      Songo's 10 EQ band gains in dB, only when its EQ is on

#include <alsa/asoundlib.h>

#include "godot_eq.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

// Most music is 44.1kHz, so this skips resampling for the common case.
// ALSA's plug layer converts if the hardware wants something else.
const int OUT_RATE = 44100;
const int OUT_CHANNELS = 2;
const unsigned ALSA_LATENCY_US = 200000;
const double ALSA_OPEN_TIMEOUT_SEC = 5.0;

// Frames mixed and written per loop, ~23ms
const int CHUNK_FRAMES = 1024;

// BTN_SELECT on most gamepads, KEY_RIGHTCTRL on Miyoo-style keyboard mappings
const int SELECT_CODES[] = { BTN_SELECT, KEY_RIGHTCTRL };
const double SELECT_HOLD_SEC = 1.0;
// Select+Left within this far into a song goes to the previous one instead
const double RESTART_THRESHOLD_SEC = 3.0;
// Select+Up/Down volume change, as a fraction of the song's own level
const float VOLUME_STEP = 0.1f;

const double NETWORK_TIMEOUT_SEC = 10.0;

volatile sig_atomic_t quit_requested = 0;

struct Playlist {
	std::vector<std::string> paths;
	int start_index = 0;
	double start_position = 0.0;
	bool repeat_one = false;
	float volume = 1.0f;
	bool eq_enabled = false;
	float eq_gains_db[GodotEQ10::BAND_COUNT] = {};
};

struct Input {
	std::vector<int> fds;
	double select_pressed_at = -1.0;
	// Set once a Select combo fires, so finishing the combo with Select still
	// held doesn't also count as the quit hold
	bool select_combo_used = false;
	// -1 previous/restart, 1 next, 0 none. Taken by the playback loop.
	int skip_request = 0;
	// Net Select+Up (+1) / Select+Down (-1) presses not yet applied
	int volume_steps = 0;
};

Input input;
snd_pcm_t *pcm = nullptr;

void on_signal(int) {
	quit_requested = 1;
}

double now_sec() {
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

bool is_select(int code) {
	for (int c : SELECT_CODES) {
		if (c == code) {
			return true;
		}
	}
	return false;
}

void open_input_devices() {
	DIR *dir = opendir("/dev/input");
	if (!dir) {
		return;
	}
	while (dirent *entry = readdir(dir)) {
		if (strncmp(entry->d_name, "event", 5) != 0) {
			continue;
		}
		std::string path = std::string("/dev/input/") + entry->d_name;
		// Read only, no grab, so the frontend and games still see every press
		int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd >= 0) {
			input.fds.push_back(fd);
		}
	}
	closedir(dir);
	fprintf(stderr, "Watching %zu input devices\n", input.fds.size());
}

// D-pad press along one axis: -1 for the negative direction (left/up), 1 for
// the positive one (right/down), 0 for anything else. Depending on the device
// the d-pad is gamepad buttons, keyboard arrows or a hat axis.
int dpad_press(const input_event &ev, int btn_neg, int btn_pos, int key_neg, int key_pos, int hat_axis) {
	if (ev.type == EV_KEY && ev.value == 1) {
		if (ev.code == btn_neg || ev.code == key_neg) {
			return -1;
		}
		if (ev.code == btn_pos || ev.code == key_pos) {
			return 1;
		}
	} else if (ev.type == EV_ABS && ev.code == hat_axis) {
		return ev.value < 0 ? -1 : ev.value > 0 ? 1 : 0;
	}
	return 0;
}

// Drains pending input events: queues a skip for Select+Left/Right and a
// volume change for Select+Up/Down, and sets quit_requested once Select has
// been held long enough on its own. Called
// often enough from the decode loop and FFmpeg's interrupt callback that a 1s
// hold is caught promptly.
void poll_input() {
	input_event ev;
	for (int fd : input.fds) {
		while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (ev.type == EV_KEY && is_select(ev.code)) {
				if (ev.value == 1) {
					input.select_pressed_at = now_sec();
					input.select_combo_used = false;
				} else if (ev.value == 0) {
					input.select_pressed_at = -1.0;
				}
				continue;
			}
			if (input.select_pressed_at < 0.0) {
				continue;
			}
			int x = dpad_press(ev, BTN_DPAD_LEFT, BTN_DPAD_RIGHT, KEY_LEFT, KEY_RIGHT, ABS_HAT0X);
			int y = dpad_press(ev, BTN_DPAD_UP, BTN_DPAD_DOWN, KEY_UP, KEY_DOWN, ABS_HAT0Y);
			if (x != 0) {
				input.skip_request = x;
				input.select_combo_used = true;
			}
			if (y != 0) {
				// Up is negative on the d-pad, but raises the volume
				input.volume_steps -= y;
				input.select_combo_used = true;
			}
		}
	}
	if (!quit_requested && input.select_pressed_at >= 0.0 && !input.select_combo_used &&
			now_sec() - input.select_pressed_at >= SELECT_HOLD_SEC) {
		fprintf(stderr, "Select held, quitting\n");
		quit_requested = 1;
	}
}

// Songo launches us with fork/exec, so we inherit whatever it had open that
// wasn't close-on-exec. Close it all so we never keep Songo's files, sockets or
// devices open after it exits. stdio is left alone, the start script sets it.
void close_inherited_fds() {
	DIR *dir = opendir("/proc/self/fd");
	if (!dir) {
		return;
	}
	std::vector<int> fds;
	while (dirent *entry = readdir(dir)) {
		int fd = atoi(entry->d_name);
		if (fd > 2 && fd != dirfd(dir)) {
			fds.push_back(fd);
		}
	}
	closedir(dir);
	for (int fd : fds) {
		close(fd);
	}
}

int interrupt_callback(void *) {
	poll_input();
	return quit_requested;
}

const char *av_err(int err) {
	static char buf[AV_ERROR_MAX_STRING_SIZE];
	av_strerror(err, buf, sizeof(buf));
	return buf;
}

// One song being decoded to interleaved S16 stereo at OUT_RATE. Pulled from by
// the main loop, so two can run at once during a blend.
class Song {
public:
	~Song() {
		av_frame_free(&frame);
		av_packet_free(&packet);
		swr_free(&swr);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
	}

	bool open(const std::string &path, double seek_sec) {
		format_ctx = avformat_alloc_context();
		if (!format_ctx) {
			return false;
		}
		format_ctx->interrupt_callback.callback = interrupt_callback;

		// Same protocol options as the GDExtension, unused ones are ignored for local files
		AVDictionary *options = nullptr;
		av_dict_set_int(&options, "rw_timeout", (int64_t)(NETWORK_TIMEOUT_SEC * 1000000.0), 0);
		av_dict_set(&options, "reconnect", "1", 0);
		av_dict_set(&options, "reconnect_streamed", "1", 0);
		av_dict_set(&options, "reconnect_on_network_error", "1", 0);
		av_dict_set(&options, "reconnect_delay_max", "5", 0);

		int ret = avformat_open_input(&format_ctx, path.c_str(), nullptr, &options);
		av_dict_free(&options);
		if (ret < 0) {
			// avformat_open_input frees the context on failure
			format_ctx = nullptr;
			fprintf(stderr, "Failed to open %s: %s\n", path.c_str(), av_err(ret));
			return false;
		}
		if ((ret = avformat_find_stream_info(format_ctx, nullptr)) < 0) {
			fprintf(stderr, "Failed to read stream info for %s: %s\n", path.c_str(), av_err(ret));
			return false;
		}

		const AVCodec *codec = nullptr;
		stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
		if (stream_index < 0 || !codec) {
			fprintf(stderr, "No audio stream in %s\n", path.c_str());
			return false;
		}
		// Skips cover art and unused HLS renditions
		for (unsigned i = 0; i < format_ctx->nb_streams; i++) {
			if ((int)i != stream_index) {
				format_ctx->streams[i]->discard = AVDISCARD_ALL;
			}
		}

		codec_ctx = avcodec_alloc_context3(codec);
		if (!codec_ctx) {
			return false;
		}
		avcodec_parameters_to_context(codec_ctx, format_ctx->streams[stream_index]->codecpar);
		if ((ret = avcodec_open2(codec_ctx, codec, nullptr)) < 0) {
			fprintf(stderr, "Failed to open decoder for %s: %s\n", path.c_str(), av_err(ret));
			return false;
		}

		AVChannelLayout out_layout;
		av_channel_layout_default(&out_layout, OUT_CHANNELS);
		ret = swr_alloc_set_opts2(&swr,
				&out_layout, AV_SAMPLE_FMT_S16, OUT_RATE,
				&codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
				0, nullptr);
		if (ret < 0 || swr_init(swr) < 0) {
			fprintf(stderr, "Failed to set up resampler for %s\n", path.c_str());
			return false;
		}

		// Live streams have no duration and can't seek
		if (format_ctx->duration != AV_NOPTS_VALUE) {
			duration = (double)format_ctx->duration / AV_TIME_BASE;
		}
		if (seek_sec > 0.0 && duration > 0.0) {
			av_seek_frame(format_ctx, -1, (int64_t)(seek_sec * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(codec_ctx);
			position_base = seek_sec;
		}

		packet = av_packet_alloc();
		frame = av_frame_alloc();
		fprintf(stderr, "Playing %s\n", path.c_str());
		return packet && frame;
	}

	// Fills up to `frames` frames. Fewer means the song has ended.
	int read(int16_t *out, int frames) {
		int filled = 0;
		while (filled < frames) {
			size_t available = (fifo.size() - fifo_pos) / OUT_CHANNELS;
			if (available == 0) {
				fifo.clear();
				fifo_pos = 0;
				if (finished || !decode_more()) {
					break;
				}
				continue;
			}
			int n = std::min((int)available, frames - filled);
			memcpy(out + filled * OUT_CHANNELS, fifo.data() + fifo_pos, (size_t)n * OUT_CHANNELS * sizeof(int16_t));
			fifo_pos += (size_t)n * OUT_CHANNELS;
			filled += n;
		}
		frames_read += (uint64_t)filled;
		return filled;
	}

	bool is_finished() const { return finished && fifo_pos >= fifo.size(); }

	// <= 0 for live streams
	double get_duration() const { return duration; }
	double get_position() const { return position_base + (double)frames_read / OUT_RATE; }

private:
	AVFormatContext *format_ctx = nullptr;
	AVCodecContext *codec_ctx = nullptr;
	SwrContext *swr = nullptr;
	AVPacket *packet = nullptr;
	AVFrame *frame = nullptr;
	int stream_index = -1;

	std::vector<int16_t> fifo;
	size_t fifo_pos = 0;
	bool demux_done = false;
	bool finished = false;
	bool first_frame = true;

	double duration = -1.0;
	double position_base = 0.0;
	uint64_t frames_read = 0;

	// Decodes until at least one frame lands in the fifo. False once the song
	// (or the decoder) is done, or on quit.
	bool decode_more() {
		while (!quit_requested) {
			int ret = avcodec_receive_frame(codec_ctx, frame);
			if (ret == 0) {
				// A backwards seek lands a little before the target, so take
				// the position from the first frame rather than the request
				if (first_frame) {
					first_frame = false;
					int64_t pts = frame->best_effort_timestamp;
					if (pts != AV_NOPTS_VALUE) {
						position_base = pts * av_q2d(format_ctx->streams[stream_index]->time_base);
					}
				}
				append_converted(frame->extended_data, frame->nb_samples);
				av_frame_unref(frame);
				return true;
			}
			if (ret == AVERROR_EOF) {
				// Whatever the resampler is still holding
				append_converted(nullptr, 0);
				finished = true;
				return fifo_pos < fifo.size();
			}
			if (ret != AVERROR(EAGAIN)) {
				finished = true;
				return false;
			}

			if (demux_done) {
				// Shouldn't happen after a flush, but don't spin if it does
				finished = true;
				return false;
			}
			ret = av_read_frame(format_ctx, packet);
			if (ret < 0) {
				demux_done = true;
				avcodec_send_packet(codec_ctx, nullptr);
				continue;
			}
			if (packet->stream_index == stream_index) {
				avcodec_send_packet(codec_ctx, packet);
			}
			av_packet_unref(packet);
		}
		return false;
	}

	void append_converted(uint8_t **in, int in_samples) {
		int max_out = swr_get_out_samples(swr, in_samples);
		if (max_out <= 0) {
			return;
		}
		size_t start = fifo.size();
		fifo.resize(start + (size_t)max_out * OUT_CHANNELS);
		uint8_t *out = (uint8_t *)(fifo.data() + start);
		int got = swr_convert(swr, &out, max_out, (const uint8_t **)in, in_samples);
		fifo.resize(start + (size_t)std::max(got, 0) * OUT_CHANNELS);
	}
};

std::string trim(const std::string &s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) {
		return "";
	}
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

bool read_playlist(const char *path, Playlist &r_playlist) {
	std::ifstream file(path);
	if (!file) {
		fprintf(stderr, "Can't open playlist %s\n", path);
		return false;
	}

	std::string dir = path;
	size_t slash = dir.find_last_of('/');
	dir = slash == std::string::npos ? "." : dir.substr(0, slash);

	std::string line;
	while (std::getline(file, line)) {
		line = trim(line);
		if (line.empty()) {
			continue;
		}
		if (line[0] == '#') {
			size_t colon = line.find(':');
			if (colon == std::string::npos) {
				continue;
			}
			std::string key = line.substr(0, colon);
			const char *value = line.c_str() + colon + 1;
			if (key == "#SONGO-START-INDEX") {
				r_playlist.start_index = atoi(value);
			} else if (key == "#SONGO-START-POSITION") {
				r_playlist.start_position = atof(value);
			} else if (key == "#SONGO-REPEAT-ONE") {
				r_playlist.repeat_one = atoi(value) != 0;
			} else if (key == "#SONGO-EQ") {
				// Comma separated dB per band, missing bands stay flat
				r_playlist.eq_enabled = true;
				const char *cursor = value;
				for (int band = 0; band < GodotEQ10::BAND_COUNT && *cursor; band++) {
					char *end = nullptr;
					r_playlist.eq_gains_db[band] = strtof(cursor, &end);
					if (end == cursor) {
						break;
					}
					cursor = *end == ',' ? end + 1 : end;
				}
			} else if (key == "#SONGO-VOLUME") {
				r_playlist.volume = std::clamp((float)atof(value), 0.0f, 1.0f);
			}
			continue;
		}
		// Relative entries are relative to the playlist, URLs and absolute paths as is
		if (line[0] != '/' && line.find("://") == std::string::npos) {
			line = dir + "/" + line;
		}
		r_playlist.paths.push_back(line);
	}

	if (r_playlist.paths.empty()) {
		fprintf(stderr, "Playlist %s has no entries\n", path);
		return false;
	}
	if (r_playlist.start_index < 0 || r_playlist.start_index >= (int)r_playlist.paths.size()) {
		r_playlist.start_index = 0;
		r_playlist.start_position = 0.0;
	}
	return true;
}

bool open_alsa() {
	// Songo starts us while it's still shutting down. Without a sound server
	// it holds the device until it exits, so keep retrying for a while.
	int err = 0;
	double give_up_at = now_sec() + ALSA_OPEN_TIMEOUT_SEC;
	while (!quit_requested) {
		err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
		if (err >= 0 || now_sec() >= give_up_at) {
			break;
		}
		usleep(100000);
	}
	if (err < 0 || quit_requested) {
		fprintf(stderr, "snd_pcm_open failed: %s\n", snd_strerror(err));
		pcm = nullptr;
		return false;
	}
	// Non-blocking was only for the open, writes should block to pace decoding
	snd_pcm_nonblock(pcm, 0);
	err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
			OUT_CHANNELS, OUT_RATE, 1, ALSA_LATENCY_US);
	if (err < 0) {
		fprintf(stderr, "snd_pcm_set_params failed: %s\n", snd_strerror(err));
		snd_pcm_close(pcm);
		pcm = nullptr;
		return false;
	}
	return true;
}

// Writes interleaved S16 frames, recovering from underruns and suspend
void write_alsa(const int16_t *samples, int frames) {
	while (frames > 0 && !quit_requested) {
		snd_pcm_sframes_t written = snd_pcm_writei(pcm, samples, frames);
		if (written < 0) {
			written = snd_pcm_recover(pcm, (int)written, 1);
			if (written < 0) {
				fprintf(stderr, "ALSA write failed: %s\n", snd_strerror((int)written));
				return;
			}
			continue;
		}
		samples += written * OUT_CHANNELS;
		frames -= (int)written;
		poll_input();
	}
}

// Scales samples by a gain ramping from `from` to `to` across the buffer, so a
// volume change doesn't click
void apply_volume(float *samples, int frames, float from, float to) {
	if (frames <= 0 || (from >= 1.0f && to >= 1.0f)) {
		return;
	}
	for (int f = 0; f < frames; f++) {
		float gain = from + (to - from) * (float)(f + 1) / (float)frames;
		for (int c = 0; c < OUT_CHANNELS; c++) {
			samples[f * OUT_CHANNELS + c] *= gain;
		}
	}
}

// The only place samples get clipped, so EQ boosts and blends can go past full
// scale on the way, like they can on Godot's float buses
void to_s16(const float *in, int16_t *out, int samples) {
	for (int i = 0; i < samples; i++) {
		out[i] = (int16_t)std::lround(std::clamp(in[i] * 32768.0f, -32768.0f, 32767.0f));
	}
}

// Walks the queue the way Songo does: repeat-one stays put, otherwise it wraps
class Queue {
public:
	explicit Queue(const Playlist &p_playlist) :
			playlist(p_playlist), index(p_playlist.start_index) {}

	int next_index(int from) const {
		return playlist.repeat_one ? from : (from + 1) % (int)playlist.paths.size();
	}

	// Opens the song at `r_index`, moving on past ones that won't open. Null
	// if nothing in the playlist plays.
	std::unique_ptr<Song> open_from(int &r_index, double seek_sec) const {
		for (size_t tries = 0; tries < playlist.paths.size() && !quit_requested; tries++) {
			auto song = std::make_unique<Song>();
			if (song->open(playlist.paths[r_index], seek_sec)) {
				return song;
			}
			seek_sec = 0.0;
			// Even on repeat-one, a song that won't open is skipped
			r_index = (r_index + 1) % (int)playlist.paths.size();
		}
		return nullptr;
	}

	const Playlist &playlist;
	int index;
};

// Plays until quit or nothing is playable. Mixes the outgoing and incoming
// songs with linear gain ramps during a blend, matching Songo's volume tweens,
// then applies Songo's EQ and the music volume. Mixing is in float from -1 to 1
// like Godot's audio frames, and only the final output is converted back.
bool play_queue(const Playlist &playlist, double blend_sec) {
	Queue queue(playlist);
	float volume = playlist.volume;
	// What the last chunk ended on, so a change ramps from there
	float applied_volume = volume;
	// Songo doesn't blend a lone song into itself unless it's repeating
	const bool can_blend = blend_sec > 0.0 && (playlist.paths.size() > 1 || playlist.repeat_one);

	std::unique_ptr<Song> current = queue.open_from(queue.index, playlist.start_position);
	if (!current) {
		fprintf(stderr, "Nothing in the playlist is playable, quitting\n");
		return false;
	}

	std::unique_ptr<Song> incoming;
	int incoming_index = -1;
	bool blend_tried = false;
	int64_t blend_frames = 0;
	int64_t blend_done = 0;

	std::vector<int16_t> a(CHUNK_FRAMES * OUT_CHANNELS);
	std::vector<int16_t> b(CHUNK_FRAMES * OUT_CHANNELS);
	std::vector<float> mix(CHUNK_FRAMES * OUT_CHANNELS);
	std::vector<int16_t> out(CHUNK_FRAMES * OUT_CHANNELS);

	// Skipped entirely when Songo's EQ is off
	std::unique_ptr<GodotEQ10> eq;
	if (playlist.eq_enabled) {
		eq = std::make_unique<GodotEQ10>(OUT_RATE);
		for (int band = 0; band < GodotEQ10::BAND_COUNT; band++) {
			eq->set_band_gain_db(band, playlist.eq_gains_db[band]);
		}
	}

	// EQ, volume, then out to ALSA, for whatever was mixed into `mix`
	auto output = [&](int frames) {
		if (eq) {
			eq->process(mix.data(), frames);
		}
		apply_volume(mix.data(), frames, applied_volume, volume);
		applied_volume = volume;
		to_s16(mix.data(), out.data(), frames * OUT_CHANNELS);
		write_alsa(out.data(), frames);
	};

	while (!quit_requested) {
		if (input.volume_steps != 0) {
			volume = std::clamp(volume + input.volume_steps * VOLUME_STEP, 0.0f, 1.0f);
			input.volume_steps = 0;
			fprintf(stderr, "Music volume %d%%\n", (int)std::lround(volume * 100.0f));
		}

		if (input.skip_request != 0) {
			int direction = input.skip_request;
			input.skip_request = 0;
			// Mid-blend the incoming song is the one being heard, so skip from it
			if (incoming) {
				current = std::move(incoming);
				queue.index = incoming_index;
			}
			int count = (int)playlist.paths.size();
			if (direction > 0) {
				queue.index = (queue.index + 1) % count;
				fprintf(stderr, "Skipping to the next song\n");
			} else if (current->get_position() >= RESTART_THRESHOLD_SEC) {
				fprintf(stderr, "Restarting the current song\n");
			} else {
				queue.index = (queue.index - 1 + count) % count;
				fprintf(stderr, "Going back to the previous song\n");
			}
			// Cut what's already queued in ALSA so the skip is heard straight away
			snd_pcm_drop(pcm);
			snd_pcm_prepare(pcm);
			current = queue.open_from(queue.index, 0.0);
			blend_tried = false;
			if (!current) {
				if (!quit_requested) {
					fprintf(stderr, "Nothing in the playlist is playable, quitting\n");
				}
				return false;
			}
		}

		// Same trigger as Songo: within the blend time of the end, capped at
		// half the song so short songs aren't mostly crossfade
		double duration = current->get_duration();
		if (can_blend && !incoming && !blend_tried && duration > 0.0) {
			double remaining = duration - current->get_position();
			if (remaining <= std::min(blend_sec, duration * 0.5)) {
				blend_tried = true;
				incoming_index = queue.next_index(queue.index);
				incoming = queue.open_from(incoming_index, 0.0);
				if (incoming) {
					blend_frames = std::max<int64_t>(1, (int64_t)(std::min(blend_sec, std::max(remaining, 0.01)) * OUT_RATE));
					blend_done = 0;
				}
			}
		}

		int got_a = current->read(a.data(), CHUNK_FRAMES);
		int frames = got_a;

		if (incoming) {
			int got_b = incoming->read(b.data(), CHUNK_FRAMES);
			std::fill(a.begin() + got_a * OUT_CHANNELS, a.end(), 0);
			std::fill(b.begin() + got_b * OUT_CHANNELS, b.end(), 0);
			frames = std::max(got_a, got_b);
			for (int f = 0; f < frames; f++) {
				float t = std::min(1.0f, (float)(blend_done + f) / (float)blend_frames);
				float gain_a = 1.0f - t;
				float gain_b = t;
				for (int c = 0; c < OUT_CHANNELS; c++) {
					int i = f * OUT_CHANNELS + c;
					mix[i] = (a[i] * gain_a + b[i] * gain_b) / 32768.0f;
				}
			}
			blend_done += frames;
			output(frames);

			// The incoming song takes over once the fade is done or the old one runs out
			if (blend_done >= blend_frames || current->is_finished()) {
				current = std::move(incoming);
				queue.index = incoming_index;
				blend_tried = false;
			}
			continue;
		}

		for (int i = 0; i < got_a * OUT_CHANNELS; i++) {
			mix[i] = a[i] / 32768.0f;
		}
		output(got_a);

		if (got_a < CHUNK_FRAMES && current->is_finished()) {
			queue.index = queue.next_index(queue.index);
			current = queue.open_from(queue.index, 0.0);
			blend_tried = false;
			if (!current) {
				if (!quit_requested) {
					fprintf(stderr, "Nothing in the playlist is playable, quitting\n");
				}
				return false;
			}
		}
		poll_input();
	}
	return true;
}

} // namespace

int main(int argc, char **argv) {
	close_inherited_fds();

	const char *playlist_path = nullptr;
	double blend_sec = 0.0;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--blend") == 0 && i + 1 < argc) {
			blend_sec = std::max(0.0, atof(argv[++i]));
		} else if (!playlist_path) {
			playlist_path = argv[i];
		}
	}
	if (!playlist_path) {
		fprintf(stderr, "Usage: %s <playlist.m3u> [--blend SECONDS]\n", argv[0]);
		return 1;
	}

	Playlist playlist;
	if (!read_playlist(playlist_path, playlist)) {
		return 1;
	}

	struct sigaction sa = {};
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, nullptr);
	sigaction(SIGINT, &sa, nullptr);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);

	av_log_set_level(AV_LOG_ERROR);
	avformat_network_init();
	open_input_devices();

	int exit_code = 0;
	if (!open_alsa()) {
		exit_code = 1;
	} else {
		fprintf(stderr, "Playing %zu songs from index %d at %.1fs, volume %.2f, blend %.1fs%s, EQ %s\n",
				playlist.paths.size(), playlist.start_index, playlist.start_position,
				playlist.volume, blend_sec, playlist.repeat_one ? ", repeating one" : "",
				playlist.eq_enabled ? "on" : "off");

		if (!play_queue(playlist, blend_sec)) {
			exit_code = 1;
		}

		if (quit_requested) {
			snd_pcm_drop(pcm);
		} else {
			snd_pcm_drain(pcm);
		}
		snd_pcm_close(pcm);
	}

	for (int fd : input.fds) {
		close(fd);
	}
	avformat_network_deinit();
	fprintf(stderr, "Exited\n");
	return exit_code;
}
