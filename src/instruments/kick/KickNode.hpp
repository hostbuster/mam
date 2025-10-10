#pragma once

#include "KickSynth.hpp"
#include "../../core/Node.hpp"
#include <cstring>

class KickNode : public Node {
public:
  explicit KickNode(const KickParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "KickNode"; }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    synth_.setSampleRate(sampleRate);
  }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    // Simple: mono -> copy to all channels
    for (uint32_t i = 0; i < ctx.frames; ++i) {
      const float s = synth_.process();
      for (uint32_t ch = 0; ch < channels; ++ch) {
        interleavedOut[i * channels + ch] = s;
      }
    }
  }

private:
  KickSynth synth_;
};



