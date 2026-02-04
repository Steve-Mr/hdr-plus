#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>

#include "src/Burst.h"
#include <align_and_merge.h>

// This test program simulates the logic inside Java_top_maary_darkbag_hdrplus_NativeHDRPlus_process
// It reads files from disk (simulating ByteBuffer inputs from Java)
// Runs the pipeline
// And writes the result to disk (simulating returning byte array to Java)

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open file: " + path);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) throw std::runtime_error("Read error");
    return buffer;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <output_dng> <input_raw1> [input_raw2 ...]" << std::endl;
        return 1;
    }

    std::string output_path = argv[1];

    // Simulate: JNI jobjectArray buffers
    std::vector<std::vector<uint8_t>> jni_buffer_storage;
    std::vector<RawBuffer> raw_buffers;

    // Load inputs
    std::cout << "[JNI-SIM] Receiving input buffers..." << std::endl;
    jni_buffer_storage.reserve(argc - 2);
    for (int i = 2; i < argc; ++i) {
        try {
            jni_buffer_storage.push_back(readFile(argv[i]));
            // Simulate: env->GetDirectBufferAddress
            raw_buffers.push_back({jni_buffer_storage.back().data(), jni_buffer_storage.back().size()});
            std::cout << "[JNI-SIM] Buffer " << (i - 2) << ": " << jni_buffer_storage.back().size() << " bytes" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    }

    try {
        // --- Start of JNI Logic Simulation ---

        // 1. Create Burst
        std::cout << "[JNI-SIM] Creating Burst..." << std::endl;
        Burst burst(raw_buffers);

        // 2. Prepare Input Buffer
        Halide::Runtime::Buffer<uint16_t> input = burst.ToBuffer();
        if (input.dimensions() != 3) {
             throw std::runtime_error("Failed to create input buffer from Burst");
        }

        // 3. Prepare Output Buffer
        Halide::Runtime::Buffer<uint16_t> output(input.width(), input.height());

        // 4. Run Pipeline
        std::cout << "[JNI-SIM] Running align_and_merge..." << std::endl;
        int result = align_and_merge(input, output);
        if (result != 0) {
            throw std::runtime_error("align_and_merge pipeline failed with error code: " + std::to_string(result));
        }

        // 5. Encode to DNG
        std::cout << "[JNI-SIM] Encoding result to DNG memory buffer..." << std::endl;
        std::vector<uint8_t> dngData;
        burst.GetRaw(0).WriteDng(dngData, output);

        // --- End of JNI Logic Simulation ---

        std::cout << "[JNI-SIM] Result size: " << dngData.size() << " bytes." << std::endl;

        // Save to disk to verify
        std::ofstream out_file(output_path, std::ios::binary);
        out_file.write(reinterpret_cast<const char*>(dngData.data()), dngData.size());
        std::cout << "[JNI-SIM] Saved to " << output_path << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[JNI-SIM] Exception caught: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
