#pragma once

#include "ClapSynth.hpp"
#include "../../core/Node.hpp"
#include "../../core/ParamIds.hpp"

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

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      synth_.trigger();
      return;
    }
    if (cmd.type == CommandType::SetParam) {
      switch (cmd.paramId) {
        case ClapParam::AMP_DECAY_MS: synth_.params().ampDecayMs = cmd.value; break;
        case ClapParam::GAIN: synth_.params().gain = cmd.value; break;
        case ClapParam::BPM: synth_.params().bpm = cmd.value; synth_.params().loop = (cmd.value > 0.0f); break;
        case ClapParam::LOOP: synth_.params().loop = (cmd.value >= 0.5f); break;
        default: break;
      }
    }
  }
private:
  ClapSynth synth_;
};



