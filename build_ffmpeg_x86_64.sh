#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFMPEG_DIR="$PROJECT_ROOT/third_party/ffmpeg"
INSTALL_DIR="$PROJECT_ROOT/build/ffmpeg/x86_64"

echo "==> FFmpeg source:  $FFMPEG_DIR"
echo "==> Install path:   $INSTALL_DIR"

cd "$FFMPEG_DIR"

echo "==> Cleaning previous FFmpeg build..."
make distclean || true

echo "==> Removing previous installed FFmpeg..."
rm -rf "$INSTALL_DIR"

echo "==> Configuring FFmpeg..."

./configure \
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
    --disable-avdevice \
    --disable-avfilter \
    --disable-swscale \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --disable-encoders \
    --disable-muxers \
    --disable-network \
    --disable-x86asm

echo "==> Building FFmpeg..."
make -j"$(nproc)"

echo "==> Installing FFmpeg..."
make install

echo
echo "==> FFmpeg build complete!"
echo
echo "Installed libraries:"
ls -lh "$INSTALL_DIR/lib/"*.a
