#pragma once

#include "ClapSynth.hpp"
#include "../../core/Node.hpp"
#include "../../core/ParamIds.hpp"
#include "../../core/ParameterRegistry.hpp"
#include "../../core/ModMatrix.hpp"

class ClapNode : public Node {
public:
  explicit ClapNode(const ClapParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "ClapNode"; }
  bool addLfo(uint16_t id, ModLfo::Wave wave, float freqHz, float phase01) { return mod_.addLfo(id, wave, freqHz, phase01); }
  bool addRoute(uint16_t sourceId, uint16_t destParamId, float depth, float offset = 0.0f) { return mod_.addRoute(sourceId, destParamId, depth, offset); }
  bool addLfoFreqRoute(uint16_t sourceId, uint16_t lfoId, float depth, float offset = 0.0f) { return mod_.addLfoFreqRoute(sourceId, lfoId, depth, offset); }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    synth_.setSampleRate(sampleRate);
    params_.prepare(sampleRate);
    params_.ensureParam(ClapParam::GAIN, nodeGain_);
    params_.ensureParam(ClapParam::AMP_DECAY_MS, synth_.params().ampDecayMs);
    params_.setSmoothing(ClapParam::GAIN, ParameterRegistry<>::Smoothing::Linear);
    params_.setSmoothing(ClapParam::AMP_DECAY_MS, ParameterRegistry<>::Smoothing::Expo);
    mod_.prepare(sampleRate);
  }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    for (uint32_t i = 0; i < ctx.frames; ++i) {
      mod_.tick();
      float baseGain = params_.next(ClapParam::GAIN);
      float baseDecay = params_.next(ClapParam::AMP_DECAY_MS);
      const float modGain = mod_.sumFor(ClapParam::GAIN);
      const float modDecay = mod_.sumFor(ClapParam::AMP_DECAY_MS);
      // Apply modulation
      nodeGain_ = baseGain + modGain; if (nodeGain_ < 0.0f) nodeGain_ = 0.0f;
      float dec = baseDecay + modDecay; if (dec < 1.0f) dec = 1.0f; synth_.params().ampDecayMs = dec;
      const float s = synth_.process();
      for (uint32_t ch = 0; ch < channels; ++ch) interleavedOut[i * channels + ch] = s * nodeGain_;
    }
  }

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      synth_.trigger();
      return;
    }
    if (cmd.type == CommandType::SetParam) {
      switch (cmd.paramId) {
        case ClapParam::AMP_DECAY_MS: params_.setImmediate(ClapParam::AMP_DECAY_MS, cmd.value); break;
        case ClapParam::GAIN: params_.setImmediate(ClapParam::GAIN, cmd.value); nodeGain_ = cmd.value; break;
        case ClapParam::BPM: synth_.params().bpm = cmd.value; synth_.params().loop = (cmd.value > 0.0f); break;
        case ClapParam::LOOP: synth_.params().loop = (cmd.value >= 0.5f); break;
        default: break;
      }
    } else if (cmd.type == CommandType::SetParamRamp) {
      if (cmd.paramId == ClapParam::GAIN) params_.rampTo(ClapParam::GAIN, cmd.value, cmd.rampMs);
      else if (cmd.paramId == ClapParam::AMP_DECAY_MS) params_.rampTo(ClapParam::AMP_DECAY_MS, cmd.value, cmd.rampMs);
    }
  }
private:
  ClapSynth synth_;
  ParameterRegistry<> params_;
  ModMatrix<> mod_;
  float nodeGain_ = 1.0f;
};



