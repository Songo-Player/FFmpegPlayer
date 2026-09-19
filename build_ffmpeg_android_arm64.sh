#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FFMPEG_DIR="$PROJECT_ROOT/third_party/ffmpeg"
INSTALL_DIR="$PROJECT_ROOT/build/ffmpeg/android/arm64"

# --------------------------------------------------
# Android NDK toolchain
# --------------------------------------------------

NDK="${ANDROID_NDK_ROOT:?set ANDROID_NDK_ROOT to your Android NDK r23c path}"
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
API=21

export PATH="$TOOLCHAIN/bin:$PATH"

echo "==> FFmpeg source:  $FFMPEG_DIR"
echo "==> NDK toolchain:  $TOOLCHAIN"
echo "==> Install path:   $INSTALL_DIR"

cd "$FFMPEG_DIR"

echo "==> Cleaning previous FFmpeg build..."
make distclean || true

echo "==> Removing previous installed FFmpeg..."
rm -rf "$INSTALL_DIR"

echo "==> Configuring FFmpeg for android-arm64 (aarch64)..."

./configure \
    --prefix="$INSTALL_DIR" \
    --enable-cross-compile \
    --target-os=android \
    --arch=aarch64 \
    --cpu=armv8-a \
    --sysroot="$TOOLCHAIN/sysroot" \
    --cc="$TOOLCHAIN/bin/clang --target=aarch64-linux-android$API" \
    --cxx="$TOOLCHAIN/bin/clang++ --target=aarch64-linux-android$API" \
    --ar="$TOOLCHAIN/bin/llvm-ar" \
    --ranlib="$TOOLCHAIN/bin/llvm-ranlib" \
    --strip="$TOOLCHAIN/bin/llvm-strip" \
    --nm="$TOOLCHAIN/bin/llvm-nm" \
    --disable-programs \
    --disable-doc \
    --disable-debug \
    --disable-shared \
    --enable-static \
    --enable-pic \
    --extra-cflags="-fPIC -O3" \
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
echo "==> FFmpeg android-arm64 build complete!"
echo
echo "Installed libraries:"
ls -lh "$INSTALL_DIR/lib/"*.a
