#!/usr/bin/env bash
set -euo pipefail

FFMPEG_REPO="https://github.com/FFmpeg/FFmpeg.git"
FFMPEG_TAG="n8.1.2"
FFMPEG_DIR="third_party/ffmpeg"

if [[ -d "$FFMPEG_DIR" ]]; then
    echo "Error: $FFMPEG_DIR already exists."
    exit 1
fi

mkdir -p "$(dirname "$FFMPEG_DIR")"

echo "Cloning FFmpeg..."
git clone "$FFMPEG_REPO" "$FFMPEG_DIR"

echo "Checking out FFmpeg $FFMPEG_TAG..."
git -C "$FFMPEG_DIR" checkout "${FFMPEG_TAG}^0"

echo "FFmpeg $FFMPEG_TAG checked out successfully."
echo "Commit: $(git -C "$FFMPEG_DIR" rev-parse HEAD)"