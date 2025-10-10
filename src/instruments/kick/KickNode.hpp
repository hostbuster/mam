#pragma once

#include "KickSynth.hpp"
#include "../../core/Node.hpp"
#include "../../core/ParamIds.hpp"
#include "../../core/ParameterRegistry.hpp"
#include <cstring>

class KickNode : public Node {
public:
  explicit KickNode(const KickParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "KickNode"; }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    synth_.setSampleRate(sampleRate);
    params_.prepare(sampleRate);
    params_.ensureParam(KickParam::GAIN, nodeGain_);
  }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    // Simple: mono -> copy to all channels
    for (uint32_t i = 0; i < ctx.frames; ++i) {
      nodeGain_ = params_.next(KickParam::GAIN);
      const float s = synth_.process();
      for (uint32_t ch = 0; ch < channels; ++ch) {
        interleavedOut[i * channels + ch] = s * nodeGain_;
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
        case KickParam::GAIN: params_.setImmediate(KickParam::GAIN, cmd.value); nodeGain_ = cmd.value; break;
        case KickParam::CLICK: synth_.params().click = cmd.value; break;
        case KickParam::BPM: synth_.params().bpm = cmd.value; synth_.params().loop = (cmd.value > 0.0f); break;
        case KickParam::LOOP: synth_.params().loop = (cmd.value >= 0.5f); break;
        default: break;
      }
    } else if (cmd.type == CommandType::SetParamRamp) {
      if (cmd.paramId == KickParam::GAIN) params_.rampTo(KickParam::GAIN, cmd.value, cmd.rampMs);
    }
  }

private:
  KickSynth synth_;
  ParameterRegistry<> params_;
  float nodeGain_ = 1.0f;
};



