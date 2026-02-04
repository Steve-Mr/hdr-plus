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

# Helper to build LibRaw with Autoconf (configure/make)
build_libraw_autoconf() {
    local NAME=$1
    local SOURCE_DIR=$2

    echo "Building $NAME (Autoconf)..."

    # Setup NDK Toolchain for Autoconf
    # We need to find the toolchain bin directory
    local HOST_TAG="linux-x86_64" # Assuming Linux Host
    local TOOLCHAIN_BIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG/bin"

    if [ ! -d "$TOOLCHAIN_BIN" ]; then
        echo "Error: Could not find NDK toolchain bin at $TOOLCHAIN_BIN"
        echo "Check your NDK installation and HOST_TAG."
        exit 1
    fi

    local TARGET_HOST="aarch64-linux-android"
    local API_LEVEL="$MIN_SDK_VERSION"

    export CC="$TOOLCHAIN_BIN/${TARGET_HOST}${API_LEVEL}-clang"
    export CXX="$TOOLCHAIN_BIN/${TARGET_HOST}${API_LEVEL}-clang++"
    export AR="$TOOLCHAIN_BIN/llvm-ar"
    export RANLIB="$TOOLCHAIN_BIN/llvm-ranlib"
    export LD="$TOOLCHAIN_BIN/ld"
    export STRIP="$TOOLCHAIN_BIN/llvm-strip"

    # Flags to include our pre-built dependencies
    export CFLAGS="-fPIC -I$INSTALL_DIR/include"
    export CXXFLAGS="-fPIC -I$INSTALL_DIR/include"
    export LDFLAGS="-L$INSTALL_DIR/lib"
    export PKG_CONFIG_PATH="$INSTALL_DIR/lib/pkgconfig"

    mkdir -p "$BUILD_DIR/$NAME"
    pushd "$BUILD_DIR/$NAME"

    # LibRaw configure usually needs to be generated or run from source
    # We copy source to build dir or run configure from source
    # Autoconf builds are often cleaner if run inside source tree or VPATH
    # Let's try VPATH (running configure from build dir)

    if [ -f "$SOURCE_DIR/configure" ]; then
        "$SOURCE_DIR/configure" \
            --host="$TARGET_HOST" \
            --prefix="$INSTALL_DIR" \
            --enable-static \
            --disable-shared \
            --disable-examples \
            --disable-openmp \
            --disable-jpeg \
            --disable-jasper \
            --disable-lcms
    elif [ -f "$SOURCE_DIR/configure.ac" ]; then
        # Need to generate configure
        echo "Generating configure script..."
        pushd "$SOURCE_DIR"
        autoreconf -if
        popd
        "$SOURCE_DIR/configure" \
            --host="$TARGET_HOST" \
            --prefix="$INSTALL_DIR" \
            --enable-static \
            --disable-shared \
            --disable-examples \
            --disable-openmp \
            --disable-jpeg \
            --disable-jasper \
            --disable-lcms
    else
        echo "Error: No configure script found in LibRaw source."
        exit 1
    fi

    make -j$(nproc)
    make install
    popd
}

# Helper to download without verify
download_file() {
    local URL=$1
    local FILENAME=$2
    local DEST="$DOWNLOAD_DIR/$FILENAME"

    if [ ! -f "$DEST" ]; then
        echo "Downloading $FILENAME..."
        wget -c "$URL" -O "$DEST"
    fi
}

# 1. ZLIB
ZLIB_VER="1.3.1"
echo "--- Processing ZLIB $ZLIB_VER ---"
download_file "https://github.com/madler/zlib/releases/download/v${ZLIB_VER}/zlib-${ZLIB_VER}.tar.gz" "zlib.tar.gz"
if [ ! -d "$DOWNLOAD_DIR/zlib-$ZLIB_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/zlib.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "zlib" "$DOWNLOAD_DIR/zlib-$ZLIB_VER" ""

# 2. libjpeg (using libjpeg-turbo for performance)
JPEG_VER="3.0.1"
echo "--- Processing libjpeg-turbo $JPEG_VER ---"
# Using official asset URL which is more stable than tag archive
download_file "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/${JPEG_VER}/libjpeg-turbo-${JPEG_VER}.tar.gz" "libjpeg.tar.gz"
if [ ! -d "$DOWNLOAD_DIR/libjpeg-turbo-$JPEG_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/libjpeg.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "libjpeg" "$DOWNLOAD_DIR/libjpeg-turbo-$JPEG_VER" \
    "-DWITH_JPEG8=1 -DENABLE_SHARED=OFF -DENABLE_STATIC=ON"

# 3. LibPNG
PNG_VER="1.6.40"
echo "--- Processing LibPNG $PNG_VER ---"
download_file "https://download.sourceforge.net/libpng/libpng-$PNG_VER.tar.gz" "libpng.tar.gz"
if [ ! -d "$DOWNLOAD_DIR/libpng-$PNG_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/libpng.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "libpng" "$DOWNLOAD_DIR/libpng-$PNG_VER" \
    "-DPNG_SHARED=OFF -DPNG_STATIC=ON -DZLIB_ROOT=$INSTALL_DIR"

# 4. LibTIFF
TIFF_VER="4.6.0"
echo "--- Processing LibTIFF $TIFF_VER ---"
download_file "https://download.osgeo.org/libtiff/tiff-$TIFF_VER.tar.gz" "tiff.tar.gz"
if [ ! -d "$DOWNLOAD_DIR/tiff-$TIFF_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/tiff.tar.gz" -C "$DOWNLOAD_DIR"
fi
build_cmake "libtiff" "$DOWNLOAD_DIR/tiff-$TIFF_VER" \
    "-DBUILD_SHARED_LIBS=OFF -DZLIB_ROOT=$INSTALL_DIR -DJPEG_ROOT=$INSTALL_DIR"

# 5. LibRaw
# Use official master tarball or tag which contains configure.ac
LIBRAW_VER="0.21.2"
echo "--- Processing LibRaw $LIBRAW_VER ---"
download_file "https://github.com/LibRaw/LibRaw/archive/refs/tags/${LIBRAW_VER}.tar.gz" "libraw.tar.gz"
if [ ! -d "$DOWNLOAD_DIR/LibRaw-$LIBRAW_VER" ]; then
    tar -xzf "$DOWNLOAD_DIR/libraw.tar.gz" -C "$DOWNLOAD_DIR"
fi

# Use Autoconf build for LibRaw
build_libraw_autoconf "libraw" "$DOWNLOAD_DIR/LibRaw-$LIBRAW_VER"

echo "=== Dependencies Built Successfully ==="
echo "Artifacts located in $INSTALL_DIR"
