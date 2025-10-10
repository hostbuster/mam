#pragma once

#include "KickSynth.hpp"
#include "../../core/Node.hpp"
#include "../../core/ParamIds.hpp"
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

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      synth_.trigger();
      return;
    }
    if (cmd.type == CommandType::SetParam) {
      switch (cmd.paramId) {
        case KickParam::F0: synth_.params().startFreqHz = cmd.value; break;
        case KickParam::FEND: synth_.params().endFreqHz = cmd.value; break;
        case KickParam::PITCH_DECAY_MS: synth_.params().pitchDecayMs = cmd.value; break;
        case KickParam::AMP_DECAY_MS: synth_.params().ampDecayMs = cmd.value; break;
        case KickParam::GAIN: synth_.params().gain = cmd.value; break;
        case KickParam::CLICK: synth_.params().click = cmd.value; break;
        case KickParam::BPM: synth_.params().bpm = cmd.value; synth_.params().loop = (cmd.value > 0.0f); break;
        case KickParam::LOOP: synth_.params().loop = (cmd.value >= 0.5f); break;
        default: break;
      }
    }
  }

private:
  KickSynth synth_;
};



