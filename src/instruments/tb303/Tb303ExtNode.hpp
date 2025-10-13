#pragma once

#include "Tb303ExtSynth.hpp"
#include "../../core/Node.hpp"
#include "../../core/ParameterRegistry.hpp"
#include "../../core/ParamIds.hpp"
#include "../../core/ModMatrix.hpp"

namespace Tb303Param {
  constexpr uint16_t WAVEFORM = 1;
  constexpr uint16_t TUNE_SEMITONES = 2;
  constexpr uint16_t GLIDE_MS = 3;
  constexpr uint16_t CUTOFF_HZ = 4;
  constexpr uint16_t RESONANCE = 5;
  constexpr uint16_t ENV_MOD = 6;
  constexpr uint16_t FILTER_DECAY_MS = 7;
  constexpr uint16_t AMP_DECAY_MS = 8;
  constexpr uint16_t AMP_GAIN = 9;
}

class Tb303ExtNode : public Node {
public:
  explicit Tb303ExtNode(const Tb303ExtParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "Tb303ExtNode"; }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    synth_.setSampleRate(sampleRate);
    params_.prepare(sampleRate);
    // Ensure parameters
    params_.ensureParam(Tb303Param::WAVEFORM, 0.0f);
    params_.ensureParam(Tb303Param::TUNE_SEMITONES, 0.0f);
    params_.ensureParam(Tb303Param::GLIDE_MS, 10.0f);
    params_.ensureParam(Tb303Param::CUTOFF_HZ, 800.0f);
    params_.ensureParam(Tb303Param::RESONANCE, 0.3f);
    params_.ensureParam(Tb303Param::ENV_MOD, 0.5f);
    params_.ensureParam(Tb303Param::FILTER_DECAY_MS, 200.0f);
    params_.ensureParam(Tb303Param::AMP_DECAY_MS, 200.0f);
    params_.ensureParam(Tb303Param::AMP_GAIN, 0.8f);
    mod_.prepare(sampleRate);
  }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    for (uint32_t i = 0; i < ctx.frames; ++i) {
      mod_.tick();
      // Pull parameter values
      synth_.params().waveform = static_cast<int>(params_.next(Tb303Param::WAVEFORM) >= 0.5f ? 1 : 0);
      synth_.params().tuneSemitones = params_.next(Tb303Param::TUNE_SEMITONES) + mod_.sumFor(Tb303Param::TUNE_SEMITONES);
      synth_.params().glideMs = params_.next(Tb303Param::GLIDE_MS);
      synth_.params().cutoffHz = params_.next(Tb303Param::CUTOFF_HZ) + mod_.sumFor(Tb303Param::CUTOFF_HZ);
      synth_.params().resonance = params_.next(Tb303Param::RESONANCE);
      synth_.params().envMod = params_.next(Tb303Param::ENV_MOD);
      synth_.params().filterDecayMs = params_.next(Tb303Param::FILTER_DECAY_MS);
      synth_.params().ampDecayMs = params_.next(Tb303Param::AMP_DECAY_MS);
      synth_.params().ampGain = params_.next(Tb303Param::AMP_GAIN);
      const float s = synth_.process();
      for (uint32_t ch = 0; ch < channels; ++ch) interleavedOut[i * channels + ch] = s;
    }
  }

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      // If trigger value is 0, use last-set VELOCITY/ACCENT from params
      const float velCmd = std::fmin(1.0f, std::fmax(0.0f, cmd.value));
      const float vel = (velCmd > 0.0f) ? velCmd : synth_.params().velocity;
      const float acc = synth_.params().accent;
      synth_.noteOn(synth_.params().noteSemitones, vel, acc);
      return;
    }
    if (cmd.type == CommandType::SetParam) {
      switch (cmd.paramId) {
        case Tb303Param::WAVEFORM: params_.setImmediate(Tb303Param::WAVEFORM, cmd.value); break;
        case Tb303Param::TUNE_SEMITONES: params_.setImmediate(Tb303Param::TUNE_SEMITONES, cmd.value); break;
        case 10 /* NOTE_SEMITONES */: synth_.params().noteSemitones = cmd.value; break;
        case 11 /* VELOCITY */: synth_.params().velocity = cmd.value; break;
        case 12 /* ACCENT */: synth_.params().accent = cmd.value; break;
        case Tb303Param::GLIDE_MS: params_.setImmediate(Tb303Param::GLIDE_MS, cmd.value); break;
        case Tb303Param::CUTOFF_HZ: params_.setImmediate(Tb303Param::CUTOFF_HZ, cmd.value); break;
        case Tb303Param::RESONANCE: params_.setImmediate(Tb303Param::RESONANCE, cmd.value); break;
        case Tb303Param::ENV_MOD: params_.setImmediate(Tb303Param::ENV_MOD, cmd.value); break;
        case Tb303Param::FILTER_DECAY_MS: params_.setImmediate(Tb303Param::FILTER_DECAY_MS, cmd.value); break;
        case Tb303Param::AMP_DECAY_MS: params_.setImmediate(Tb303Param::AMP_DECAY_MS, cmd.value); break;
        case Tb303Param::AMP_GAIN: params_.setImmediate(Tb303Param::AMP_GAIN, cmd.value); break;
        // MIDI CC simulation
        case 101 /* CC1 */: synth_.params().envMod = cmd.value; break;
        case 102 /* CC74 */: synth_.params().cutoffHz = cmd.value * 18000.0f; break;
        case 103 /* CC71 */: synth_.params().resonance = cmd.value; break;
        case 104 /* CC7  */: synth_.params().ampGain = cmd.value; break;
        case 105 /* PITCH_BEND */: synth_.params().tuneSemitones = cmd.value * 2.0f; break;
        default: break;
      }
    } else if (cmd.type == CommandType::SetParamRamp) {
      switch (cmd.paramId) {
        case Tb303Param::TUNE_SEMITONES: params_.rampTo(Tb303Param::TUNE_SEMITONES, cmd.value, cmd.rampMs); break;
        case Tb303Param::CUTOFF_HZ: params_.rampTo(Tb303Param::CUTOFF_HZ, cmd.value, cmd.rampMs); break;
        case Tb303Param::GLIDE_MS: params_.rampTo(Tb303Param::GLIDE_MS, cmd.value, cmd.rampMs); break;
        default: break;
      }
    }
  }

  // Expose LFO routing
  bool addLfo(uint16_t id, ModLfo::Wave wave, float freqHz, float phase01) { return mod_.addLfo(id, wave, freqHz, phase01); }
  bool addRoute(uint16_t sourceId, uint16_t destParamId, float depth, float offset = 0.0f) { return mod_.addRoute(sourceId, destParamId, depth, offset); }
  bool addLfoFreqRoute(uint16_t sourceId, uint16_t lfoId, float depth, float offset = 0.0f) { return mod_.addLfoFreqRoute(sourceId, lfoId, depth, offset); }
  bool addRouteWithRange(uint16_t sourceId, uint16_t destParamId, float minV, float maxV, typename ModMatrix<>::Route::Map map) { return mod_.addRouteWithRange(sourceId, destParamId, minV, maxV, map); }

private:
  Tb303ExtSynth synth_;
  ParameterRegistry<> params_;
  ModMatrix<> mod_;
};


