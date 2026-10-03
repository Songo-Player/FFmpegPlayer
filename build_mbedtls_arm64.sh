#!/usr/bin/env bash

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MBEDTLS_DIR="$PROJECT_ROOT/third_party/mbedtls"
INSTALL_DIR="$PROJECT_ROOT/build/mbedtls/arm64"
BUILD_DIR="$PROJECT_ROOT/build/mbedtls/arm64-cmake"

echo "==> mbedTLS source: $MBEDTLS_DIR"
echo "==> Install path:   $INSTALL_DIR"

echo "==> Removing previous mbedTLS build..."
rm -rf "$BUILD_DIR" "$INSTALL_DIR"

echo "==> Configuring mbedTLS..."

# Static, PIC libraries only: they get linked into FFmpeg's static libs
# and from there into the GDExtension shared library.
cmake -S "$MBEDTLS_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
    -DENABLE_PROGRAMS=OFF \
    -DENABLE_TESTING=OFF \
    -DGEN_FILES=OFF \
    -DMBEDTLS_FATAL_WARNINGS=OFF

echo "==> Building mbedTLS..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Installing mbedTLS..."
cmake --install "$BUILD_DIR"

echo
echo "==> mbedTLS build complete!"
echo
echo "Installed libraries:"
ls -lh "$INSTALL_DIR/lib/"*.a
