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
  constexpr uint16_t DRIVE = 13;
}

class Tb303ExtNode : public Node {
public:
  explicit Tb303ExtNode(const Tb303ExtParams& p) : synth_(p, 48000.0) {}
  const char* name() const override { return "Tb303ExtNode"; }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    synth_.setSampleRate(sampleRate);
    ctxSampleRate_ = sampleRate;
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
    params_.ensureParam(Tb303Param::DRIVE, 0.0f);
    // ADSR optional params (defaults preserve legacy behavior)
    params_.ensureParam(200 /* ENV_MODE */, 0.0f);
    params_.ensureParam(201 /* FILTER_ATTACK_MS */, 0.0f);
    params_.ensureParam(202 /* FILTER_SUSTAIN */, 0.0f);
    params_.ensureParam(203 /* FILTER_RELEASE_MS */, 200.0f);
    params_.ensureParam(204 /* AMP_ATTACK_MS */, 0.0f);
    params_.ensureParam(205 /* AMP_SUSTAIN */, 0.7f);
    params_.ensureParam(206 /* AMP_RELEASE_MS */, 200.0f);
    params_.ensureParam(207 /* GATE_LEN_MS */, 120.0f);
    mod_.prepare(sampleRate);
  }
  void reset() override { synth_.reset(); }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    ctxSampleRate_ = ctx.sampleRate;
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
      synth_.params().drive = params_.next(Tb303Param::DRIVE);
      // ADSR optional
      synth_.params().envMode = params_.current(200);
      synth_.params().filterAttackMs = params_.current(201);
      synth_.params().filterSustain = params_.current(202);
      synth_.params().filterReleaseMs = params_.current(203);
      synth_.params().ampAttackMs = params_.current(204);
      synth_.params().ampSustain = params_.current(205);
      synth_.params().ampReleaseMs = params_.current(206);
      synth_.params().gateLenMs = params_.current(207);
      // Filter algo/type/keytracking
      synth_.params().filterAlgo = params_.current(300);
      synth_.params().filterType = params_.current(301);
      synth_.params().keytrack = params_.current(302);
      float s = synth_.process();
      // Optional equal-power pan when channels >= 2
      float pan = params_.current(14 /* PAN */);
      if (pan < -1.0f) pan = -1.0f; else if (pan > 1.0f) pan = 1.0f;
      if (channels >= 2) {
        const float l = std::cos(0.25f * 3.14159265f * (pan + 1.0f));
        const float r = std::sin(0.25f * 3.14159265f * (pan + 1.0f));
        interleavedOut[i * channels + 0] = s * l;
        interleavedOut[i * channels + 1] = s * r;
        for (uint32_t ch = 2; ch < channels; ++ch) interleavedOut[i * channels + ch] = s * 0.5f;
      } else {
        for (uint32_t ch = 0; ch < channels; ++ch) interleavedOut[i * channels + ch] = s;
      }
    }
  }

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      // If trigger value is 0, use last-set VELOCITY/ACCENT from params; default velocity to 0.8 if never set
      const float velCmd = std::fmin(1.0f, std::fmax(0.0f, cmd.value));
      float vel = (velCmd > 0.0f) ? velCmd : synth_.params().velocity;
      if (vel <= 0.0f) vel = 0.8f;
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
        case Tb303Param::DRIVE: params_.setImmediate(Tb303Param::DRIVE, cmd.value); break;
        case 200 /* ENV_MODE */: params_.setImmediate(200, cmd.value); break;
        case 201 /* FILTER_ATTACK_MS */: params_.setImmediate(201, cmd.value); break;
        case 202 /* FILTER_SUSTAIN */: params_.setImmediate(202, cmd.value); break;
        case 203 /* FILTER_RELEASE_MS */: params_.setImmediate(203, cmd.value); break;
        case 204 /* AMP_ATTACK_MS */: params_.setImmediate(204, cmd.value); break;
        case 205 /* AMP_SUSTAIN */: params_.setImmediate(205, cmd.value); break;
        case 206 /* AMP_RELEASE_MS */: params_.setImmediate(206, cmd.value); break;
        case 207 /* GATE_LEN_MS */: params_.setImmediate(207, cmd.value); break;
        case 300 /* FILTER_ALGO */: params_.setImmediate(300, cmd.value); break;
        case 301 /* FILTER_TYPE */: params_.setImmediate(301, cmd.value); break;
        case 302 /* KEYTRACK */: params_.setImmediate(302, cmd.value); break;
        // MIDI CC simulation
        case 101 /* CC1 */: synth_.params().envMod = cmd.value; break;
        case 102 /* CC74 */: synth_.params().cutoffHz = cmd.value * 18000.0f; break;
        case 103 /* CC71 */: synth_.params().resonance = cmd.value; break;
        case 104 /* CC7  */: synth_.params().ampGain = cmd.value; break;
        case 105 /* PITCH_BEND */: synth_.params().tuneSemitones = cmd.value * 2.0f; break;
        case 106 /* LFO1_FREQ_HZ */: mod_.addLfo(1, ModLfo::Wave::Sine, cmd.value, 0.0f); break;
        case 107 /* LFO2_FREQ_HZ */: mod_.addLfo(2, ModLfo::Wave::Sine, cmd.value, 0.0f); break;
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
  ParameterRegistry<12> params_;
  ModMatrix<> mod_;
  double ctxSampleRate_ = 48000.0;
};


