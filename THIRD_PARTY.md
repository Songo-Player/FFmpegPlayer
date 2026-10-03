# Third-Party Software

## FFmpeg

FFmpeg is used by FFmpegPlayer under the terms of the
GNU Lesser General Public License version 3 or later (LGPLv3+).

FFmpeg is built with `--enable-version3` because it links against
mbedTLS (Apache-2.0), which FFmpeg only permits under the (L)GPLv3.
See licenses/ffmpeg/ for the license texts and build configuration.

FFmpeg website:
https://ffmpeg.org/

FFmpeg version:
n8.1.2

The FFmpeg source corresponding to the distributed binaries is
available at:

SongoPlayer org repo link, add later

The FFmpeg build used by this project is configured without GPL
or nonfree components.

## Mbed TLS

Mbed TLS provides TLS for https:// streams. It is used by FFmpegPlayer
under the terms of the Apache License 2.0 (Mbed TLS is dual-licensed
Apache-2.0 OR GPL-2.0-or-later; this project uses it under Apache-2.0).

Mbed TLS website:
https://www.trustedfirmware.org/projects/mbed-tls/

Mbed TLS version:
v3.6.7

Source:
https://github.com/Mbed-TLS/mbedtls

See licenses/mbedtls/LICENSE for the license text.

## Godot Engine

The background player's equalizer (bgplayer/godot_eq.h) is ported from
Godot Engine's AudioEffectEQ10 (servers/audio/effects/eq_filter.cpp and
audio_effect_eq.cpp), used under the terms of the MIT License.

Godot website:
https://godotengine.org/

Godot version:
4.3-stable

Source:
https://github.com/godotengine/godot

See licenses/godot/LICENSE.txt for the license text.