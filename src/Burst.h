#pragma once

#include "InputSource.h"

#include <Halide.h>

#include <string>
#include <vector>

struct RawBuffer {
  const void *data;
  size_t size;
};

class Burst {
public:
  Burst(std::string dir_path, std::vector<std::string> inputs);
  Burst(const std::vector<RawBuffer> &inputs);

  ~Burst() = default;

  int GetWidth() const { return Raws.empty() ? -1 : Raws[0].GetWidth(); }

  int GetHeight() const { return Raws.empty() ? -1 : Raws[0].GetHeight(); }

  int GetBlackLevel() const {
    return Raws.empty() ? -1 : Raws[0].GetScalarBlackLevel();
  }

  int GetWhiteLevel() const {
    return Raws.empty() ? -1 : Raws[0].GetWhiteLevel();
  }

  WhiteBalance GetWhiteBalance() const {
    return Raws.empty() ? WhiteBalance{-1, -1, -1, -1}
                        : Raws[0].GetWhiteBalance();
  }

  CfaPattern GetCfaPattern() const {
    return Raws.empty() ? CfaPattern::CFA_UNKNOWN : Raws[0].GetCfaPattern();
  }

  Halide::Runtime::Buffer<float> GetColorCorrectionMatrix() const {
    return Raws.empty() ? Halide::Runtime::Buffer<float>()
                        : Raws[0].GetColorCorrectionMatrix();
  }

  Halide::Runtime::Buffer<uint16_t> ToBuffer() const;

  void CopyToBuffer(Halide::Runtime::Buffer<uint16_t> &buffer) const;

  const RawImage &GetRaw(const size_t i) const;

private:
  std::string Dir;
  std::vector<std::string> Inputs;
  std::vector<RawImage> Raws;

private:
  static std::vector<RawImage> LoadRaws(const std::string &dirPath,
                                        const std::vector<std::string> &inputs);
  static std::vector<RawImage> LoadRaws(const std::vector<RawBuffer> &inputs);
};
