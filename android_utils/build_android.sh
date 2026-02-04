#!/bin/bash
set -e

# Configuration
NDK_PATH=${ANDROID_NDK_ROOT:-$ANDROID_NDK_HOME}

if [ -z "$NDK_PATH" ]; then
    echo "Error: ANDROID_NDK_ROOT (or ANDROID_NDK_HOME) is not set."
    exit 1
fi

# Determine project root relative to script location
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
WORK_DIR="$SCRIPT_DIR"

ANDROID_BUILD_DIR="$WORK_DIR/build/android_project"
HOST_BUILD_DIR="$WORK_DIR/build/host_generator"
GENERATED_DIR="$WORK_DIR/generated"
# Dependencies installed by build_deps.sh
DEPS_INSTALL_DIR="$WORK_DIR/install"

mkdir -p "$ANDROID_BUILD_DIR"
mkdir -p "$HOST_BUILD_DIR"
mkdir -p "$GENERATED_DIR"

echo "=== HDR+ Android Build Script ==="
echo "Project Root: $PROJECT_ROOT"

# Check for Halide Location
CMAKE_EXTRA_ARGS=""
if [ -n "$HALIDE_DISTRIB_DIR" ]; then
    echo "Using HALIDE_DISTRIB_DIR: $HALIDE_DISTRIB_DIR"
    CMAKE_EXTRA_ARGS="-DHALIDE_DISTRIB_DIR=$HALIDE_DISTRIB_DIR"
fi

# Step 1: Build Halide Generator on Host
# We assume the host environment has CMake, Clang/GCC, etc.
echo "--> Building Halide Generator on Host..."
# -S points to source root (PROJECT_ROOT)
cmake -S "$PROJECT_ROOT" -B "$HOST_BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    $CMAKE_EXTRA_ARGS

cmake --build "$HOST_BUILD_DIR" --target align_and_merge_generator -j$(nproc)

# Step 2: Generate Halide Object Files for Android
echo "--> Generating Halide Object Files for arm64-v8a..."
GENERATOR_EXEC="$HOST_BUILD_DIR/align_and_merge_generator"

# Check if generator exists
if [ ! -f "$GENERATOR_EXEC" ]; then
    echo "Error: Generator executable not found at $GENERATOR_EXEC"
    exit 1
fi

# Run generator
# We generate a static library (.a) and header (.h)
# target=arm-64-android corresponds to typical Android ARM64
"$GENERATOR_EXEC" \
    -g align_and_merge \
    -o "$GENERATED_DIR" \
    target=arm-64-android

echo "Generated files:"
ls -l "$GENERATED_DIR"

# Step 3: Build Android Shared Library
echo "--> Building libhdrplus_jni.so for Android..."

# We pass specific flags to CMake to tell it we are in Android mode
# and where to find the pre-compiled Halide files and dependencies.
cmake -S "$PROJECT_ROOT" -B "$ANDROID_BUILD_DIR" \
    -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="arm64-v8a" \
    -DANDROID_PLATFORM="android-26" \
    -DANDROID_STL=c++_shared \
    -DHALIDE_GEN_DIR="$GENERATED_DIR" \
    -DANDROID_DEPS_ROOT="$DEPS_INSTALL_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    $CMAKE_EXTRA_ARGS

cmake --build "$ANDROID_BUILD_DIR" --target hdrplus_jni -j$(nproc)

echo "=== Build Success ==="
echo "Output: $ANDROID_BUILD_DIR/libhdrplus_jni.so"
echo "Don't forget to run android_utils/build_deps.sh first if you haven't!"
