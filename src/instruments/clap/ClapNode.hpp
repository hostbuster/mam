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
  bool addLfoPhaseRoute(uint16_t sourceId, uint16_t lfoId, float depth, float offset = 0.0f) { return mod_.addLfoPhaseRoute(sourceId, lfoId, depth, offset); }
  bool addRouteWithRange(uint16_t sourceId, uint16_t destParamId, float minV, float maxV, typename ModMatrix<>::Route::Map map) { return mod_.addRouteWithRange(sourceId, destParamId, minV, maxV, map); }
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
      float basePan = params_.current(ClapParam::PAN);
      const float modGain = mod_.sumFor(ClapParam::GAIN);
      const float modDecay = mod_.sumFor(ClapParam::AMP_DECAY_MS);
      // Apply modulation
      nodeGain_ = baseGain + modGain; if (nodeGain_ < 0.0f) nodeGain_ = 0.0f;
      float dec = baseDecay + modDecay; if (dec < 1.0f) dec = 1.0f; synth_.params().ampDecayMs = dec;
      const float s = synth_.process();
      // Simple equal-power pan
      float pan = basePan; if (pan < -1.0f) pan = -1.0f; else if (pan > 1.0f) pan = 1.0f;
      float l = std::cos(0.25f * 3.14159265f * (pan + 1.0f));
      float r = std::sin(0.25f * 3.14159265f * (pan + 1.0f));
      if (channels >= 2) {
        interleavedOut[i * channels + 0] = s * nodeGain_ * l;
        interleavedOut[i * channels + 1] = s * nodeGain_ * r;
        for (uint32_t ch = 2; ch < channels; ++ch) interleavedOut[i * channels + ch] = s * nodeGain_ * 0.5f;
      } else {
        interleavedOut[i * channels + 0] = s * nodeGain_;
      }
    }
  }

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      synth_.trigger(params_.current(ClapParam::VELOCITY));
      return;
    }
    if (cmd.type == CommandType::SetParam) {
      switch (cmd.paramId) {
        case ClapParam::AMP_DECAY_MS: params_.setImmediate(ClapParam::AMP_DECAY_MS, cmd.value); break;
        case ClapParam::GAIN: params_.setImmediate(ClapParam::GAIN, cmd.value); nodeGain_ = cmd.value; break;
        case ClapParam::PAN: params_.setImmediate(ClapParam::PAN, cmd.value); break;
        case ClapParam::VELOCITY: params_.setImmediate(ClapParam::VELOCITY, cmd.value); break;
        case ClapParam::BPM: synth_.params().bpm = cmd.value; synth_.params().loop = (cmd.value > 0.0f); break;
        case ClapParam::LOOP: synth_.params().loop = (cmd.value >= 0.5f); break;
        case ClapParam::LFO1_FREQ_HZ: mod_.addLfo(1, ModLfo::Wave::Sine, cmd.value, 0.0f); break;
        case ClapParam::LFO2_FREQ_HZ: mod_.addLfo(2, ModLfo::Wave::Sine, cmd.value, 0.0f); break;
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



