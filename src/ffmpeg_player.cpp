#include "ffmpeg_player.h"

#include <godot_cpp/classes/audio_stream_playback.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

void FFMPEGPlayer::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));

	ClassDB::bind_method(D_METHOD("test_ffmpeg"), &FFMPEGPlayer::test_ffmpeg);
	ClassDB::bind_method(D_METHOD("play", "path", "seek_time"), &FFMPEGPlayer::play, DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop"), &FFMPEGPlayer::stop);
	ClassDB::bind_method(D_METHOD("pause"), &FFMPEGPlayer::pause);
	ClassDB::bind_method(D_METHOD("resume"), &FFMPEGPlayer::resume);
	ClassDB::bind_method(D_METHOD("seek", "seconds"), &FFMPEGPlayer::seek);
	ClassDB::bind_method(D_METHOD("is_paused"), &FFMPEGPlayer::is_paused);
	ClassDB::bind_method(D_METHOD("is_playing"), &FFMPEGPlayer::is_playing);
	ClassDB::bind_method(D_METHOD("get_playback_position"), &FFMPEGPlayer::get_playback_position);
	ClassDB::bind_method(D_METHOD("get_generator"), &FFMPEGPlayer::get_generator);
	ClassDB::bind_method(D_METHOD("get_player"), &FFMPEGPlayer::get_player);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "generator", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamGenerator"), "", "get_generator");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player", PROPERTY_HINT_RESOURCE_TYPE, "AudioStreamPlayer"), "", "get_player");
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
	generator->set_buffer_length(0.2);

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

void FFMPEGPlayer::play(String path, double seek_time) {
	if (playing) {
		stop();
	}

	current_path = path;
	frames_played = 0;
	playing = true;

	// Open format context
    int open_ret = avformat_open_input(&format_ctx, path.utf8().get_data(), nullptr, nullptr);
    if (open_ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(open_ret, errbuf, sizeof(errbuf));
        UtilityFunctions::print("Failed to open file: ", path, " ffmpeg error: ", errbuf);
        playing = false;
        return;
    }
	avformat_find_stream_info(format_ctx, nullptr);

	// Find audio stream
	audio_stream_index = -1;
	for (unsigned i = 0; i < format_ctx->nb_streams; i++) {
		if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_index = i;
			break;
		}
	}
	if (audio_stream_index == -1) {
		UtilityFunctions::print("No audio stream found");
		playing = false;
		return;
	}

	// Set up codec
	AVCodecParameters *codecpar = format_ctx->streams[audio_stream_index]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
	codec_ctx = avcodec_alloc_context3(codec);
	avcodec_parameters_to_context(codec_ctx, codecpar);
	avcodec_open2(codec_ctx, codec, nullptr);

	// Set up resampler to stereo float 44100
	AVChannelLayout out_layout;
	av_channel_layout_default(&out_layout, 2);
	swr_alloc_set_opts2(
			&swr,
			&out_layout, AV_SAMPLE_FMT_FLT, 44100,
			&codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
			0, nullptr);
	swr_init(swr);

	// Seek if requested
	if (seek_time > 0.0) {
		int64_t ts = (int64_t)(seek_time * AV_TIME_BASE);
		av_seek_frame(format_ctx, -1, ts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(codec_ctx);
	}

    player->play();

	Ref<AudioStreamPlayback> pb = player->get_stream_playback();
	playback = Ref<AudioStreamGeneratorPlayback>(Object::cast_to<AudioStreamGeneratorPlayback>(pb.ptr()));

	decode_thread = std::thread(_decode_thread_func, this);
}

void FFMPEGPlayer::_decode_thread_func(FFMPEGPlayer *self) {
	if (!self) {
		return;
	}

	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	std::vector<float> pcm_buffer;
	pcm_buffer.resize(2048 * 2);

	while (self->playing) {
		if (self->paused) {
			OS::get_singleton()->delay_usec(5000);
			continue;
		}

		int read_ret = av_read_frame(self->format_ctx, packet);

		if (read_ret < 0) {
			if (!self->playing) {
				break;
			}
			if (read_ret == AVERROR_EOF) {
				break;
			}
			// Transient error, retry
			UtilityFunctions::print("av_read_frame error: ", read_ret);
			continue;
		}

		if (packet->stream_index != self->audio_stream_index) {
			av_packet_unref(packet);
			continue;
		}

		avcodec_send_packet(self->codec_ctx, packet);

		while (self->playing && avcodec_receive_frame(self->codec_ctx, frame) == 0) {
			// Resize buffer if needed
			size_t needed = (size_t)frame->nb_samples * 2; // *2 for stereo
			if (pcm_buffer.size() < needed) {
				pcm_buffer.resize(needed);
			}

			float *pcm_ptr = pcm_buffer.data();

			int out_samples = swr_convert(
					self->swr,
					(uint8_t **)&pcm_ptr,
					frame->nb_samples,
					(const uint8_t **)frame->extended_data,
					frame->nb_samples);

			int written = 0;

			while (written < out_samples && self->playing) {
				while (self->playing && self->playback.is_valid() && !self->playback->can_push_buffer(1)) {
					OS::get_singleton()->delay_usec(200);
				}

				if (!self->playback.is_valid()) {
					break;
				}

				Vector2 frame_data(pcm_buffer[written * 2], pcm_buffer[written * 2 + 1]);
				self->playback->push_frame(frame_data);

				{
					std::lock_guard<std::mutex> lock(self->mutex);
					self->frames_played++;
				}
				written++;
			}
		}

		av_packet_unref(packet);
	}

	av_frame_free(&frame);
	av_packet_free(&packet);

	if (self->playing) {
		self->call_deferred("emit_signal", "finished");
		self->stream_finished = true;
	}
	self->playing = false;
}

double FFMPEGPlayer::get_playback_position() const {
	std::lock_guard<std::mutex> lock(mutex);

	if (!generator.is_valid()) {
		return 0.0;
	}

	return (double)frames_played / generator->get_mix_rate();
}

void FFMPEGPlayer::stop() {
	playing = false;
	paused = false;

	if (decode_thread.joinable()) {
		decode_thread.join();
	}

	stream_finished = false;

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

void FFMPEGPlayer::seek(double seconds) {
	if (!format_ctx || audio_stream_index < 0 || !generator.is_valid()) {
		return;
	}

	String path = current_path;
	stop();
	play(path, seconds);

	std::lock_guard<std::mutex> lock(mutex);
	frames_played = (uint64_t)(seconds * generator->get_mix_rate());
}