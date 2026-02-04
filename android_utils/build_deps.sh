#!/bin/bash
set -e

# Configuration
NDK_PATH=${ANDROID_NDK_ROOT:-$ANDROID_NDK_HOME}

if [ -z "$NDK_PATH" ]; then
    echo "Error: ANDROID_NDK_ROOT (or ANDROID_NDK_HOME) is not set."
    echo "Please set it to your Android NDK installation path."
    exit 1
fi

ABI="arm64-v8a"
MIN_SDK_VERSION=26
TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"

# Use absolute paths relative to script location
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORK_DIR="$SCRIPT_DIR"
INSTALL_DIR="$WORK_DIR/install"
BUILD_DIR="$WORK_DIR/build"
DOWNLOAD_DIR="$WORK_DIR/downloads"

mkdir -p "$INSTALL_DIR"
mkdir -p "$BUILD_DIR"
mkdir -p "$DOWNLOAD_DIR"

echo "=== Android Dependency Builder ==="
echo "NDK: $NDK_PATH"
echo "ABI: $ABI"
echo "Install Prefix: $INSTALL_DIR"

# Helper function to build with CMake
build_cmake() {
    local NAME=$1
    local SOURCE_DIR=$2
    local EXTRA_ARGS=$3

    echo "Building $NAME..."
    mkdir -p "$BUILD_DIR/$NAME"
    pushd "$BUILD_DIR/$NAME"

    cmake "$SOURCE_DIR" \
        -G "Unix Makefiles" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM="android-$MIN_SDK_VERSION" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        $EXTRA_ARGS

    cmake --build . --target install --config Release -j$(nproc)
    popd
}

# Helper to download and verify
download_and_verify() {
    local URL=$1
    local FILENAME=$2
    local SHA256=$3
    local DEST="$DOWNLOAD_DIR/$FILENAME"

    if [ ! -f "$DEST" ]; then
        echo "Downloading $FILENAME..."
        wget -c "$URL" -O "$DEST"
    fi

    echo "$SHA256  $DEST" | sha256sum -c - || {
        echo "Checksum failed for $FILENAME"
        rm -f "$DEST"
        exit 1
    }
}

# 1. ZLIB
ZLIB_VER="1.3.1"
ZLIB_SHA256="9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
echo "--- Processing ZLIB $ZLIB_VER ---"
download_and_verify "https://www.zlib.net/zlib-$ZLIB_VER.tar.gz" "zlib.tar.gz" "$ZLIB_SHA256"
if [ ! -d "$DOWNLOAD_DIR/zlib-$ZLIB_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/zlib.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "zlib" "$DOWNLOAD_DIR/zlib-$ZLIB_VER" ""

# 2. libjpeg (using libjpeg-turbo for performance)
JPEG_VER="3.0.1"
# SHA256 for libjpeg-turbo-3.0.1.tar.gz from github tags
JPEG_SHA256="22429507714ce147b84dd2e96301c70dd49912176251b1451f2512a101f68133"
echo "--- Processing libjpeg-turbo $JPEG_VER ---"
download_and_verify "https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/$JPEG_VER.tar.gz" "libjpeg.tar.gz" "$JPEG_SHA256"
if [ ! -d "$DOWNLOAD_DIR/libjpeg-turbo-$JPEG_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/libjpeg.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "libjpeg" "$DOWNLOAD_DIR/libjpeg-turbo-$JPEG_VER" \
    "-DWITH_JPEG8=1 -DENABLE_SHARED=OFF -DENABLE_STATIC=ON"

# 3. LibPNG
PNG_VER="1.6.40"
PNG_SHA256="535b479b2467ff231a3ec6d92a525906fb8ef27978be4f66dbe05d3f3a01b3a1"
echo "--- Processing LibPNG $PNG_VER ---"
download_and_verify "https://download.sourceforge.net/libpng/libpng-$PNG_VER.tar.gz" "libpng.tar.gz" "$PNG_SHA256"
if [ ! -d "$DOWNLOAD_DIR/libpng-$PNG_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/libpng.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "libpng" "$DOWNLOAD_DIR/libpng-$PNG_VER" \
    "-DPNG_SHARED=OFF -DPNG_STATIC=ON -DZLIB_ROOT=$INSTALL_DIR"

# 4. LibTIFF
TIFF_VER="4.6.0"
TIFF_SHA256="88b391607480e20f815e6ce9acd2258b1c37b69f1e181d7d508e25091807d9b9"
echo "--- Processing LibTIFF $TIFF_VER ---"
download_and_verify "https://download.osgeo.org/libtiff/tiff-$TIFF_VER.tar.gz" "tiff.tar.gz" "$TIFF_SHA256"
if [ ! -d "$DOWNLOAD_DIR/tiff-$TIFF_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/tiff.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "libtiff" "$DOWNLOAD_DIR/tiff-$TIFF_VER" \
    "-DBUILD_SHARED_LIBS=OFF -DZLIB_ROOT=$INSTALL_DIR -DJPEG_ROOT=$INSTALL_DIR"

# 5. LibRaw
LIBRAW_VER="0.21.2"
LIBRAW_SHA256="7c49b657e28b24479e390c2b2923c5017415e9a4f447732a3962d3858c67c57d"
echo "--- Processing LibRaw $LIBRAW_VER ---"
download_and_verify "https://www.libraw.org/data/LibRaw-$LIBRAW_VER.tar.gz" "libraw.tar.gz" "$LIBRAW_SHA256"
if [ ! -d "$DOWNLOAD_DIR/LibRaw-$LIBRAW_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/libraw.tar.gz" -C "$DOWNLOAD_DIR"
fi
# LibRaw cmake might need finding ZLIB/JPEG manually if not standard
build_cmake "libraw" "$DOWNLOAD_DIR/LibRaw-$LIBRAW_VER" \
    "-DBUILD_SHARED_LIBS=OFF -DENABLE_EXAMPLES=OFF -DENABLE_RAWSPEED=OFF -DENABLE_LIBRAW_CUDA=OFF -DENABLE_OPENMP=OFF -DZLIB_ROOT=$INSTALL_DIR -DJPEG_ROOT=$INSTALL_DIR"

echo "=== Dependencies Built Successfully ==="
echo "Artifacts located in $INSTALL_DIR"
