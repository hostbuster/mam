#pragma once

#include "KickSynth.hpp"
#include "../../core/Node.hpp"
#include "../../core/ParamIds.hpp"
#include "../../core/ParameterRegistry.hpp"
#include <cstring>
#include "../../core/ModMatrix.hpp"

class KickNode : public Node {
public:
  explicit KickNode(const KickParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "KickNode"; }
  // Public hooks for factory to configure modulation without RTTI outside
  bool addLfo(uint16_t id, ModLfo::Wave wave, float freqHz, float phase01) { return mod_.addLfo(id, wave, freqHz, phase01); }
  bool addRoute(uint16_t sourceId, uint16_t destParamId, float depth, float offset = 0.0f) { return mod_.addRoute(sourceId, destParamId, depth, offset); }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    synth_.setSampleRate(sampleRate);
    params_.prepare(sampleRate);
    params_.ensureParam(KickParam::GAIN, nodeGain_);
    params_.ensureParam(KickParam::F0, synth_.params().startFreqHz);
    params_.ensureParam(KickParam::FEND, synth_.params().endFreqHz);
    params_.ensureParam(KickParam::PITCH_DECAY_MS, synth_.params().pitchDecayMs);
    params_.ensureParam(KickParam::AMP_DECAY_MS, synth_.params().ampDecayMs);
    // Smoothing modes: gain linear; decays exponential; freqs linear
    params_.setSmoothing(KickParam::GAIN, ParameterRegistry<>::Smoothing::Linear);
    params_.setSmoothing(KickParam::PITCH_DECAY_MS, ParameterRegistry<>::Smoothing::Expo);
    params_.setSmoothing(KickParam::AMP_DECAY_MS, ParameterRegistry<>::Smoothing::Expo);
    mod_.prepare(sampleRate);
  }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    // Simple: mono -> copy to all channels
    for (uint32_t i = 0; i < ctx.frames; ++i) {
      mod_.tick();
      // Smooth params
      nodeGain_ = params_.next(KickParam::GAIN);
      {
        const float baseF0 = params_.next(KickParam::F0);
        const float modF0 = baseF0 + mod_.sumFor(KickParam::F0);
        synth_.params().startFreqHz = modF0;
      }
      synth_.params().endFreqHz = params_.next(KickParam::FEND);
      synth_.params().pitchDecayMs = params_.next(KickParam::PITCH_DECAY_MS);
      synth_.params().ampDecayMs = params_.next(KickParam::AMP_DECAY_MS);
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
        case KickParam::F0: params_.setImmediate(KickParam::F0, cmd.value); break;
        case KickParam::FEND: params_.setImmediate(KickParam::FEND, cmd.value); break;
        case KickParam::PITCH_DECAY_MS: params_.setImmediate(KickParam::PITCH_DECAY_MS, cmd.value); break;
        case KickParam::AMP_DECAY_MS: params_.setImmediate(KickParam::AMP_DECAY_MS, cmd.value); break;
        case KickParam::GAIN: params_.setImmediate(KickParam::GAIN, cmd.value); nodeGain_ = cmd.value; break;
        case KickParam::CLICK: synth_.params().click = cmd.value; break;
        case KickParam::BPM: synth_.params().bpm = cmd.value; synth_.params().loop = (cmd.value > 0.0f); break;
        case KickParam::LOOP: synth_.params().loop = (cmd.value >= 0.5f); break;
        default: break;
      }
    } else if (cmd.type == CommandType::SetParamRamp) {
      switch (cmd.paramId) {
        case KickParam::GAIN: params_.rampTo(KickParam::GAIN, cmd.value, cmd.rampMs); break;
        case KickParam::F0: params_.rampTo(KickParam::F0, cmd.value, cmd.rampMs); break;
        case KickParam::FEND: params_.rampTo(KickParam::FEND, cmd.value, cmd.rampMs); break;
        case KickParam::PITCH_DECAY_MS: params_.rampTo(KickParam::PITCH_DECAY_MS, cmd.value, cmd.rampMs); break;
        case KickParam::AMP_DECAY_MS: params_.rampTo(KickParam::AMP_DECAY_MS, cmd.value, cmd.rampMs); break;
        default: break;
      }
    }
  }

private:
  KickSynth synth_;
  ParameterRegistry<> params_;
  ModMatrix<> mod_;
  float nodeGain_ = 1.0f;
};



