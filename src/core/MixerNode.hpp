#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include "Node.hpp"

struct MixerChannel { std::string id; float gain = 1.0f; };

class MixerNode : public Node {
public:
  explicit MixerNode(std::vector<MixerChannel> channels, float masterGain = 1.0f, bool softClip = true)
  : channels_(std::move(channels)), masterGain_(masterGain), softClip_(softClip) {}

  const char* name() const override { return "MixerNode"; }
  void prepare(double, uint32_t) override {}
  void reset() override {}

  // For now, this mixer expects that previous nodes have already written to interleavedOut.
  // We scale in-place by per-channel gains (mono to all channels) and then apply master + soft clip.
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    // Per-input scaling is handled upstream (Graph can pre-apply), here we apply master and soft clip only.
    const size_t total = static_cast<size_t>(ctx.frames) * channels;
    if (masterGain_ != 1.0f) {
      for (size_t i = 0; i < total; ++i) interleavedOut[i] *= masterGain_;
    }
    if (softClip_) {
      for (size_t i = 0; i < total; ++i) {
        // tanh soft clipper
        interleavedOut[i] = std::tanh(interleavedOut[i]);
      }
    }
  }

  const std::vector<MixerChannel>& channels() const { return channels_; }
  float masterGain() const { return masterGain_; }
  bool softClip() const { return softClip_; }

private:
  std::vector<MixerChannel> channels_;
  float masterGain_ = 1.0f;
  bool softClip_ = true;
};



