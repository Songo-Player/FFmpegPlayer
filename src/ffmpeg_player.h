#ifndef FFMPEG_PLAYER_H
#define FFMPEG_PLAYER_H

#include <godot_cpp/classes/node.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace godot {

class FFMPEGPlayer : public Node {
	GDCLASS(FFMPEGPlayer, Node)

protected:
	static void _bind_methods();

public:
	FFMPEGPlayer();
	~FFMPEGPlayer();

	void test_ffmpeg();
};

} // namespace godot

#endif // FFMPEG_PLAYER_H