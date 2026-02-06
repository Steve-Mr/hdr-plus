cmake_minimum_required(VERSION 3.22)
project(HDR_PLUS LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define dependencies
if(DEFINED HALIDE_DISTRIB_DIR)
  list(APPEND CMAKE_PREFIX_PATH ${HALIDE_DISTRIB_DIR})
endif()

if(MSVC)
    add_compile_definitions("NOMINMAX")
endif()

set(src_files
    src/InputSource.cpp
    src/Burst.cpp
    src/LibRaw2DngConverter.cpp)

set(header_files
    src/InputSource.h
    src/Burst.h
    src/LibRaw2DngConverter.h)

if(ANDROID)
    # --- Android Build Configuration ---

    # Dependencies (Dependencies are pre-built by android_utils/build_deps.sh)
    if(DEFINED ANDROID_DEPS_ROOT)
        list(APPEND CMAKE_PREFIX_PATH ${ANDROID_DEPS_ROOT})
        set(CMAKE_FIND_ROOT_PATH ${ANDROID_DEPS_ROOT})
        set(HDRPLUS_EXTRA_INCLUDE_DIRS
            ${ANDROID_DEPS_ROOT}/include
        )
    endif()

    find_package(Halide REQUIRED)
    # Get Halide include directory from the package
    get_target_property(HALIDE_INCLUDE_DIR Halide::Halide INTERFACE_INCLUDE_DIRECTORIES)

    find_library(LIBRAW_LIBRARY NAMES raw raw_r)
    find_library(TIFF_LIBRARY NAMES tiff)
    find_library(JPEG_LIBRARY NAMES jpeg turbojpeg)
    find_library(PNG_LIBRARY NAMES png)
    find_library(ZLIB_LIBRARY NAMES z)

    find_library(LOG_LIB log)
    find_library(ANDROID_LIB android)
    find_library(JNIGRAPHICS_LIB jnigraphics)

    # Import the pre-generated Halide library (from Host Step)
    if(NOT DEFINED HALIDE_GEN_DIR)
        message(FATAL_ERROR "HALIDE_GEN_DIR must be defined for Android build")
    endif()

    add_library(align_and_merge STATIC IMPORTED)
    set_target_properties(align_and_merge PROPERTIES
        IMPORTED_LOCATION "${HALIDE_GEN_DIR}/align_and_merge.a"
        INTERFACE_INCLUDE_DIRECTORIES "${HALIDE_GEN_DIR}"
    )

    set(HDRPLUS_COMMON_LIBS
        align_and_merge
        ${LIBRAW_LIBRARY}
        ${TIFF_LIBRARY}
        ${JPEG_LIBRARY}
        ${PNG_LIBRARY}
        ${ZLIB_LIBRARY}
        ${LOG_LIB}
        ${ANDROID_LIB}
        ${JNIGRAPHICS_LIB}
    )

else()
    # --- Host / Linux Build Configuration ---

    find_package(Halide REQUIRED)
    find_package(TIFF REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(PNG REQUIRED)
    find_package(JPEG REQUIRED)
    find_library(LIBRAW_LIBRARY NAMES raw raw_r)
    find_library(TIFFXX_LIBRARY NAMES tiffxx)

    add_executable(hdrplus_pipeline_generator src/hdrplus_pipeline_generator.cpp src/align.cpp src/merge.cpp src/finish.cpp src/util.cpp)
    target_include_directories(hdrplus_pipeline_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(hdrplus_pipeline_generator PRIVATE Halide::Generator)
    add_halide_library(hdrplus_pipeline
        FROM hdrplus_pipeline_generator
        FUNCTION_NAME hdrplus_pipeline
    )

    add_executable(align_and_merge_generator src/align_and_merge_generator.cpp src/align.cpp src/merge.cpp src/util.cpp)
    target_include_directories(align_and_merge_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(align_and_merge_generator PRIVATE Halide::Generator)
    add_halide_library(align_and_merge
        FROM align_and_merge_generator
        FUNCTION_NAME align_and_merge
    )

    add_executable(hdrplus bin/HDRPlus.cpp ${src_files})
    target_include_directories(hdrplus PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/genfiles)
    add_dependencies(hdrplus hdrplus_pipeline)
    target_link_libraries(hdrplus PRIVATE hdrplus_pipeline Halide::Halide PNG::PNG ${LIBRAW_LIBRARY} TIFF::TIFF ${TIFFXX_LIBRARY})

    add_executable(stack_frames bin/stack_frames.cpp ${src_files})
    target_include_directories(stack_frames PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/genfiles)
    add_dependencies(stack_frames align_and_merge)
    target_link_libraries(stack_frames PRIVATE Halide::Halide align_and_merge ${LIBRAW_LIBRARY} PNG::PNG JPEG::JPEG TIFF::TIFF ${TIFFXX_LIBRARY})

    add_executable(test_buffer_io bin/test_buffer_io.cpp ${src_files})
    target_include_directories(test_buffer_io PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/genfiles)
    add_dependencies(test_buffer_io align_and_merge)
    target_link_libraries(test_buffer_io PRIVATE Halide::Halide align_and_merge ${LIBRAW_LIBRARY} PNG::PNG JPEG::JPEG TIFF::TIFF ${TIFFXX_LIBRARY})

    # Host Simulation setup for JNI
    find_package(JNI REQUIRED)

    set(HDRPLUS_COMMON_LIBS
        align_and_merge
        Halide::Halide
        ${LIBRAW_LIBRARY}
        TIFF::TIFF
        ${TIFFXX_LIBRARY}
    )

    set(HDRPLUS_EXTRA_INCLUDE_DIRS
        ${CMAKE_BINARY_DIR}/genfiles
        ${JNI_INCLUDE_DIRS}
    )

    # Test JNI Simulation
    add_executable(test_jni_simulation bin/test_jni_simulation.cpp ${src_files})
    target_include_directories(test_jni_simulation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/genfiles
    )
    add_dependencies(test_jni_simulation align_and_merge)
    target_link_libraries(test_jni_simulation PRIVATE ${HDRPLUS_COMMON_LIBS})

endif()

# --- Common JNI Library Definition ---
# This block runs for both Android and Host builds
# It expects HDRPLUS_COMMON_LIBS and HDRPLUS_EXTRA_INCLUDE_DIRS (optional) to be set by the blocks above

if(NOT DEFINED HDRPLUS_EXTRA_INCLUDE_DIRS)
    if(ANDROID)
        set(HDRPLUS_EXTRA_INCLUDE_DIRS
            ${HALIDE_GEN_DIR}
            ${HALIDE_INCLUDE_DIR}
        )
    endif()
endif()

add_library(hdrplus_jni SHARED
    src/jni/native_hdr_plus_jni.cpp
    ${src_files}
)

target_include_directories(hdrplus_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${HDRPLUS_EXTRA_INCLUDE_DIRS}
)

target_link_libraries(hdrplus_jni PRIVATE ${HDRPLUS_COMMON_LIBS})
