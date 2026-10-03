#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFMPEG_DIR="$PROJECT_ROOT/third_party/ffmpeg"
INSTALL_DIR="$PROJECT_ROOT/build/ffmpeg/x86_64"

# mbedTLS provides https:// support; build it first with build_mbedtls_*.sh
MBEDTLS_INSTALL_DIR="$PROJECT_ROOT/build/mbedtls/x86_64"

echo "==> FFmpeg source:  $FFMPEG_DIR"
echo "==> Install path:   $INSTALL_DIR"
echo "==> mbedTLS:        $MBEDTLS_INSTALL_DIR"

if [ ! -f "$MBEDTLS_INSTALL_DIR/lib/libmbedtls.a" ]; then
    echo "mbedTLS not found, run the matching build_mbedtls_*.sh first" >&2
    exit 1
fi

# Audio-only component lists, shared with the other build_ffmpeg_*.sh scripts
source "$PROJECT_ROOT/ffmpeg_audio_components.sh"

# Only let configure see our static mbedTLS, never a system copy.
export PKG_CONFIG_LIBDIR="$MBEDTLS_INSTALL_DIR/lib/pkgconfig"

cd "$FFMPEG_DIR"

echo "==> Cleaning previous FFmpeg build..."
make distclean || true

echo "==> Removing previous installed FFmpeg..."
rm -rf "$INSTALL_DIR"

echo "==> Configuring FFmpeg..."

./configure \
    --pkg-config-flags="--static" \
    --prefix="$INSTALL_DIR" \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-shared \
    --enable-static \
    --enable-pic \
    --extra-cflags="-fPIC" \
    --disable-gpl \
    --disable-nonfree \
    --enable-version3 \
    --disable-avdevice \
    --disable-avfilter \
    --disable-swscale \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --disable-encoders \
    --disable-decoders \
    --enable-decoder="$FFMPEG_AUDIO_DECODERS" \
    --disable-parsers \
    --enable-parser="$FFMPEG_AUDIO_PARSERS" \
    --disable-demuxer="$FFMPEG_IMAGE_DEMUXERS" \
    --disable-muxers \
    --enable-network \
    --enable-mbedtls \
    --disable-x86asm \
    --disable-zlib \
    --disable-bzlib \
    --disable-lzma \
    --disable-vaapi \
    --disable-vdpau \
    --disable-libdrm \
    --disable-xlib 

echo "==> Building FFmpeg..."
make -j"$(nproc)"

echo "==> Installing FFmpeg..."
make install

echo
echo "==> FFmpeg build complete!"
echo
echo "Installed libraries:"
ls -lh "$INSTALL_DIR/lib/"*.a
