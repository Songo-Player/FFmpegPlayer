#!/usr/bin/env bash
set -euo pipefail

GODOT_CPP_REPO="https://github.com/godotengine/godot-cpp.git"
GODOT_CPP_BRANCH="4.3"
GODOT_CPP_DIR="godot-cpp"

if [[ -d "$GODOT_CPP_DIR" ]]; then
    echo "Error: $GODOT_CPP_DIR already exists."
    exit 1
fi

mkdir -p "$(dirname "$GODOT_CPP_DIR")"

echo "Cloning godot-cpp branch $GODOT_CPP_BRANCH..."
git clone \
    --branch "$GODOT_CPP_BRANCH" \
    "$GODOT_CPP_REPO" \
    "$GODOT_CPP_DIR"

echo "godot-cpp cloned successfully."
echo "Commit: $(git -C "$GODOT_CPP_DIR" rev-parse HEAD)"