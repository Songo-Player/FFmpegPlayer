#!/usr/bin/env bash

# Builds the standalone background player (bgplayer/songo_bgplayer.cpp) against
# the same static FFmpeg/mbedTLS as the extension. Run on (or in a chroot of)
# the target architecture, after build_mbedtls_*.sh and build_ffmpeg_*.sh.
# Needs ALSA headers (libasound2-dev); libasound itself is linked dynamically
# since every CFW ships it.
#
#   ./build_bgplayer.sh [arm64|x86_64]   (default arm64)

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH="${1:-arm64}"
FFMPEG_DIR="$PROJECT_ROOT/build/ffmpeg/$ARCH"
MBEDTLS_DIR="$PROJECT_ROOT/build/mbedtls/$ARCH"
OUT="$PROJECT_ROOT/bin/songo_bgplayer.linux.$ARCH"

if [ ! -f "$FFMPEG_DIR/lib/libavformat.a" ]; then
    echo "FFmpeg not found in $FFMPEG_DIR, run the matching build_ffmpeg_*.sh first" >&2
    exit 1
fi

echo "==> Building $OUT"

# libstdc++/libgcc are static so the binary doesn't depend on the CFW's versions
g++ -std=c++17 -O2 -Wall \
    -I"$FFMPEG_DIR/include" \
    "$PROJECT_ROOT/bgplayer/songo_bgplayer.cpp" \
    -o "$OUT" \
    "$FFMPEG_DIR/lib/libavformat.a" \
    "$FFMPEG_DIR/lib/libavcodec.a" \
    "$FFMPEG_DIR/lib/libswresample.a" \
    "$FFMPEG_DIR/lib/libavutil.a" \
    "$MBEDTLS_DIR/lib/libmbedtls.a" \
    "$MBEDTLS_DIR/lib/libmbedx509.a" \
    "$MBEDTLS_DIR/lib/libmbedcrypto.a" \
    -lasound -lpthread -lm -ldl \
    -static-libstdc++ -static-libgcc

strip "$OUT"

echo "==> Done"
ls -lh "$OUT"
