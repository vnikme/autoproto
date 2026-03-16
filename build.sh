#!/bin/bash
set -e

BUILD_DIR="build"
BUILD_TYPE="${BUILD_TYPE:-Release}"
TARGET="${1:-}"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..
if [ -n "$TARGET" ]; then
  cmake --build . --target "$TARGET" -j"$(nproc)"
else
  cmake --build . -j"$(nproc)"
fi
