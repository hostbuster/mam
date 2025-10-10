#pragma once

#include <vector>
#include <cstdint>

template <typename Synth>
struct OfflineRenderer {
  static std::vector<float> renderInterleaved(Synth& synth, uint32_t sampleRate, uint32_t channels, uint64_t frames) {
    (void)sampleRate; // synth should already know SR
    std::vector<float> out;
    out.resize(static_cast<size_t>(frames * channels));
    for (uint64_t i = 0; i < frames; ++i) {
      const float s = synth.process();
      for (uint32_t ch = 0; ch < channels; ++ch) {
        out[static_cast<size_t>(i * channels + ch)] = s;
      }
    }
    return out;
  }
};



