# Android Integration Guide

This document describes how to build the `libhdrplus_jni.so` library for Android and integrate it into your application.

## Prerequisites

*   Linux Host Machine (or MacOS/WSL)
*   Android NDK (r25+ recommended)
*   CMake (3.22+)
*   Java Development Kit (JDK)
*   Build tools: `make`, `wget`, `tar`

## Build Instructions

The build process is split into two stages:
1.  **Dependencies**: Building static libraries (LibRaw, TIFF, etc.) for Android.
2.  **Project Build**: Generating Halide code on the host and cross-compiling the shared library.

### 1. Build Dependencies

Run the dependency build script once. This downloads and builds required libraries.

```bash
export ANDROID_NDK_ROOT=/path/to/your/ndk
./android_utils/build_deps.sh
```

This will create `android_utils/install/` containing the compiled libraries.

### 2. Build Shared Library

Run the main build script. This will first compile the Halide generator on your host machine, generate the ARM64 assembly, and then link everything into an Android shared library.

```bash
export ANDROID_NDK_ROOT=/path/to/your/ndk
./android_utils/build_android.sh
```

**Output:** `android_utils/build/android_project/libhdrplus_jni.so`

## Integration into Android Studio

### 1. Add Native Library

Copy the generated `.so` file to your Android Studio project's `jniLibs` directory.

```
app/src/main/jniLibs/arm64-v8a/libhdrplus_jni.so
```

### 2. Add Java Interface

Copy the Java interface file `src/java/top/maary/darkbag/hdrplus/NativeHDRPlus.java` to your source set.

```
app/src/main/java/top/maary/darkbag/hdrplus/NativeHDRPlus.java
```

### 3. Usage

```java
import top.maary.darkbag.hdrplus.NativeHDRPlus;
import java.nio.ByteBuffer;

// ...

// Load raw file content into Direct ByteBuffers
ByteBuffer[] buffers = new ByteBuffer[burstSize];
for (int i = 0; i < burstSize; i++) {
    buffers[i] = loadFileToDirectBuffer(files[i]);
}

try {
    // Process
    byte[] dngData = NativeHDRPlus.process(buffers);

    // Save dngData to file
    saveToFile(dngData, "output.dng");

} catch (RuntimeException e) {
    e.printStackTrace();
}
```

**Note:** The input `ByteBuffer`s **must** be direct buffers (`ByteBuffer.allocateDirect` or mapped from file).
