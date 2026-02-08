# Android Optimization & Integration Guide

This document outlines the changes made to the `hdr-plus` repository to optimize performance for Android devices (ARM64/NEON) and integrate the specific pipeline required by the Android application.

## 1. New Pipeline Generator

A new Halide generator has been added: `src/hdrplus_raw_pipeline_generator.cpp`.

*   **Target Name:** `hdrplus_raw_pipeline`
*   **Purpose:** This generator implements the full raw processing pipeline (`align` -> `merge` -> `demosaic` -> `chroma_denoise` -> `srgb`) expected by the Android JNI layer.
*   **Optimizations:**
    *   **Inline Scheduling:** Helper functions like `demosaic`, `white_balance`, and `bilateral_filter` include optimized Halide schedules directly in the generator source.
    *   **Tiling:** Uses 2D tiling (e.g., 256x128 blocks) to maximize L2 cache hits on mobile SoCs.
    *   **Vectorization:** Vectorization widths are set to 8 (for `uint16`) and 4 (for `float`) to perfectly match ARM NEON 128-bit registers, reducing register pressure and spilling compared to the default desktop schedules.

## 2. Core Schedule Optimizations

The core alignment and merging algorithms in `src/align.cpp` and `src/merge.cpp` have been optimized. **Note:** These changes affect all build targets in this repository.

*   **Tiling:** The `align` and `merge_temporal`/`merge_spatial` functions now use explicit 2D tiling.
    *   *Benefit:* Reduced memory bandwidth usage by keeping the working set of "hot" tiles within the smaller system caches of mobile processors.
*   **Vectorization:**
    *   Vectorization factors changed from `16` or `32` to `8`.
    *   *Benefit:* Aligns with the 8-element limit of NEON vectors for 16-bit integers. This prevents the compiler from generating inefficient loop unrolling or scalar fallback code.

## 3. Building for Android

To build the `hdrplus_raw_pipeline` library for Android, you must use the Android NDK toolchain file with CMake.

**Example Build Command:**

```bash
mkdir build-android && cd build-android
cmake -G "Unix Makefiles" \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-29 \
      -DHALIDE_DISTRIB_DIR=/path/to/halide-android-distrib \
      ..

# Build the specific target used by the Android app
make hdrplus_raw_pipeline
```

This will produce `libhdrplus_raw_pipeline.a` (or `.so`) which can be linked into your Android project.

## 4. Integration Notes

*   **Replacing the Original Repo:** You can safely replace the original `hdr-plus` source code in your build system with this version. The `align.h` and `merge.h` C++ interfaces remain unchanged, ensuring source compatibility.
*   **Performance:** Expect significantly faster processing times on ARM devices due to the cache-friendly tiling and NEON-optimized vectorization.
