#!/bin/bash
# Direct ESP-IDF build using PlatformIO's toolchain
# Bypasses PlatformIO SCons wrapper, uses CMake+ninja directly

set -e

PROJECT_DIR="/home/tcvdog/桌面/esp32rlcd/vendor/02_ESP-IDF/07_Audio_Test"
IDF_PATH="/home/tcvdog/.platformio/packages/framework-espidf"
BUILD_DIR="$PROJECT_DIR/.pio/build/esp32-s3-devkitc-1"
TOOLCHAIN="$IDF_PATH/tools/cmake/toolchain-esp32s3.cmake"
CMAKE="/home/tcvdog/.platformio/packages/tool-cmake/bin/cmake"
NINJA="/home/tcvdog/.platformio/packages/tool-ninja/ninja"
EMBED_SCRIPT="$IDF_PATH/tools/cmake/scripts/data_file_embed_asm.cmake"

# Clean
rm -rf "$BUILD_DIR" "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.old"
mkdir -p "$BUILD_DIR"

# Run CMake configure
export IDF_PATH
export PATH="$IDF_PATH/tools:$PATH"

"$CMAKE" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DPYTHON_DEPS_CHECKED=1 \
  -DESP_PLATFORM=1 \
  -DIDF_VER=v5.5.3 \
  -DCCACHE_ENABLE=0 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -S "$PROJECT_DIR" \
  -B "$BUILD_DIR" 2>&1

echo "=== CMake configure done ==="

# Pre-generate embedded .S files
echo "=== Generating embedded files ==="
"$CMAKE" -D DATA_FILE="$PROJECT_DIR/components/ExternLib/codec_board/board_cfg.txt" \
  -D SOURCE_FILE="$BUILD_DIR/board_cfg.txt.S" \
  -D FILE_TYPE=TEXT \
  -P "$EMBED_SCRIPT"

"$CMAKE" -D DATA_FILE="$PROJECT_DIR/components/port_bsp/pcm/canon.pcm" \
  -D SOURCE_FILE="$BUILD_DIR/canon.pcm.S" \
  -D FILE_TYPE=BINARY \
  -P "$EMBED_SCRIPT"

echo "=== Running ninja build ==="
"$NINJA" -C "$BUILD_DIR" 2>&1
echo "=== Build complete ==="
