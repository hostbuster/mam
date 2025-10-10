#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>

struct AudioBuffer {
  uint32_t channels = 0;
  uint32_t frames = 0;
  std::vector<float> data; // interleaved: [f0_c0, f0_c1, ..., f1_c0, ...]

  void allocate(uint32_t numChannels, uint32_t numFrames) {
    channels = numChannels;
    frames = numFrames;
    data.assign(static_cast<size_t>(channels) * static_cast<size_t>(frames), 0.0f);
  }

  void zero() {
    std::fill(data.begin(), data.end(), 0.0f);
  }

  inline float* framePtr(uint32_t frameIndex) {
    return data.data() + static_cast<size_t>(frameIndex) * channels;
  }
};



