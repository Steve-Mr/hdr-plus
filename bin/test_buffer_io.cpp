#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include "src/Burst.h"
#include <align_and_merge.h>

// Helper to wrap the Halide pipeline
Halide::Runtime::Buffer<uint16_t>
align_and_merge_helper(Halide::Runtime::Buffer<uint16_t> burst) {
  if (burst.channels() < 2) {
    return {};
  }
  Halide::Runtime::Buffer<uint16_t> merged_buffer(burst.width(),
                                                  burst.height());
  // The generated Halide function
  align_and_merge(burst, merged_buffer);
  return merged_buffer;
}

// Helper to read file to buffer
std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open file: " + path);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!file.read((char*)buffer.data(), size)) throw std::runtime_error("Read error");
    return buffer;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <output_dng> <input_raw1> [input_raw2 ...]" << std::endl;
        return 1;
    }

    std::string output_path = argv[1];
    std::vector<std::vector<uint8_t>> file_buffers;
    std::vector<RawBuffer> raw_buffers;

    // Load inputs into memory
    std::cout << "Loading inputs..." << std::endl;
    file_buffers.reserve(argc - 2);
    for (int i = 2; i < argc; ++i) {
        try {
            file_buffers.push_back(readFile(argv[i]));
            std::cout << "Loaded " << argv[i] << " (" << file_buffers.back().size() << " bytes)" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
    }

    for (const auto &buffer : file_buffers) {
      raw_buffers.push_back({buffer.data(), buffer.size()});
    }

    // Process using in-memory APIs
    try {
        std::cout << "Creating Burst..." << std::endl;
        Burst burst(raw_buffers);

        std::cout << "Running align_and_merge..." << std::endl;
        Halide::Runtime::Buffer<uint16_t> merged = align_and_merge_helper(burst.ToBuffer());

        std::cout << "Writing result to memory buffer..." << std::endl;
        std::vector<uint8_t> output_dng_data;
        burst.GetRaw(0).WriteDng(output_dng_data, merged);

        std::cout << "Saving " << output_dng_data.size() << " bytes to disk: " << output_path << std::endl;
        std::ofstream out_file(output_path, std::ios::binary);
        out_file.write((const char*)output_dng_data.data(), output_dng_data.size());

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
