#pragma once

#include "ClapSynth.hpp"
#include "../../core/Node.hpp"

class ClapNode : public Node {
public:
  explicit ClapNode(const ClapParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "ClapNode"; }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override { synth_.setSampleRate(sampleRate); }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    for (uint32_t i = 0; i < ctx.frames; ++i) {
      const float s = synth_.process();
      for (uint32_t ch = 0; ch < channels; ++ch) interleavedOut[i * channels + ch] = s;
    }
  }
private:
  ClapSynth synth_;
};



