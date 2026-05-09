#!/bin/bash
set -e

PROJECT_DIR="/home/tcvdog/桌面/esp32rlcd/vendor/02_ESP-IDF/07_Audio_Test"
BUILD_DIR="$PROJECT_DIR/.pio/build/esp32-s3-devkitc-1"
IDF_PATH="/home/tcvdog/.platformio/packages/framework-espidf"
CMAKE="/home/tcvdog/.platformio/packages/tool-cmake/bin/cmake"
NINJA="/home/tcvdog/.platformio/packages/tool-ninja/ninja"
ESPPYTHON="/home/tcvdog/.platformio/penv/.espidf-5.5.3/bin/python"
ESPPORT="/home/tcvdog/.platformio/packages/tool-esptoolpy"

export IDF_PATH
export CMAKE_MAKE_PROGRAM="$NINJA"
export PATH="$IDF_PATH/tools:$PATH"
export PYTHONPATH="$ESPPORT:$PYTHONPATH"

# 1. Clean
rm -rf "$BUILD_DIR" "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.old"
mkdir -p "$BUILD_DIR"
cd "$PROJECT_DIR"

# 2. CMake configure
echo "=== CMake configure ==="
"$CMAKE" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_BUILD_TYPE=Release \
  -DESP_PLATFORM=1 \
  -DPYTHON_DEPS_CHECKED=1 \
  -S "$PROJECT_DIR" \
  -B "$BUILD_DIR" 2>&1 || true

# 3. Pre-generate embedded files
echo "=== Pre-generate embedded files ==="
"$CMAKE" -D DATA_FILE="$PROJECT_DIR/components/ExternLib/codec_board/board_cfg.txt" \
  -D SOURCE_FILE="$BUILD_DIR/board_cfg.txt.S" \
  -D FILE_TYPE=TEXT \
  -P "$IDF_PATH/tools/cmake/scripts/data_file_embed_asm.cmake"

"$CMAKE" -D DATA_FILE="$PROJECT_DIR/components/port_bsp/pcm/canon.pcm" \
  -D SOURCE_FILE="$BUILD_DIR/canon.pcm.S" \
  -D FILE_TYPE=BINARY \
  -P "$IDF_PATH/tools/cmake/scripts/data_file_embed_asm.cmake"

# 4. Ninja build
echo "=== Ninja build ==="
"$NINJA" -C "$BUILD_DIR" -j4 2>&1

echo "=== BUILD COMPLETE ==="
ls -la "$BUILD_DIR"/07_Audio_Test.bin 2>/dev/null || ls -la "$BUILD_DIR"/07_Audio_Test.elf 2>/dev/null
