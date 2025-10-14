#pragma once

#include "../../core/Node.hpp"
#include "../../core/ParamMap.hpp"
#include "../../core/ParameterRegistry.hpp"
#include <cmath>

class MamChipNode : public Node {
public:
  MamChipNode() { initParams(); }

  const char* name() const override { return "mam_chip"; }

  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    params_.prepare(sr_);
    phase_ = 0.0f; env_ = 0.0f; envStage_ = Stage::Idle;
  }
  void reset() override { phase_ = 0.0f; env_ = 0.0f; envStage_ = Stage::Idle; }

  void handleEvent(const Command& cmd) override {
    if (cmd.type == CommandType::Trigger) {
      // Start envelope
      envStage_ = Stage::Attack; env_ = 0.0f; envT_ = 0.0f;
      return;
    }
    if (cmd.type == CommandType::SetParam) params_.setImmediate(cmd.paramId, cmd.value);
    else if (cmd.type == CommandType::SetParamRamp) params_.rampTo(cmd.paramId, cmd.value, cmd.rampMs);
  }

  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    const uint32_t n = ctx.frames;
    if (channels == 0 || n == 0) return;
    const float note = params_.current(2); // NOTE_SEMITONES
    const float gain = params_.current(5);
    const float pan = params_.current(6);
    const float pw = params_.current(4);
    const int wave = static_cast<int>(params_.current(1) + 0.5f);
    const float noiseMix = params_.current(11);

    const float freq = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
    const float phaseInc = static_cast<float>(freq / sr_);
    const float panL = std::cos(0.25f * static_cast<float>(M_PI) * (pan + 1.0f));
    const float panR = std::sin(0.25f * static_cast<float>(M_PI) * (pan + 1.0f));

    for (uint32_t i = 0; i < n; ++i) {
      // Envelope (simple ADSR)
      stepEnvelope();
      float osc = generateWave(wave, pw);
      // White noise (very simple)
      float nz = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
      float s = (1.0f - noiseMix) * osc + noiseMix * nz;
      s *= env_ * gain;
      if (channels == 1) {
        interleavedOut[i] = s;
      } else {
        interleavedOut[2 * static_cast<size_t>(i)    ] = s * panL;
        interleavedOut[2 * static_cast<size_t>(i) + 1] = s * panR;
      }
      phase_ += phaseInc; if (phase_ >= 1.0f) phase_ -= 1.0f;
    }
  }

private:
  enum class Stage { Idle, Attack, Decay, Sustain, Release };
  void stepEnvelope() {
    const float attMs = params_.current(7);
    const float decMs = params_.current(8);
    const float sus = params_.current(9);
    const float relMs = params_.current(10);
    const float dt = 1000.0f / static_cast<float>(sr_);
    switch (envStage_) {
      case Stage::Idle: env_ = 0.0f; break;
      case Stage::Attack: env_ += (dt / std::max(1e-3f, attMs)); if (env_ >= 1.0f) { env_ = 1.0f; envStage_ = Stage::Decay; } break;
      case Stage::Decay: env_ -= (dt / std::max(1e-3f, decMs)) * (1.0f - sus); if (env_ <= sus) { env_ = sus; envStage_ = Stage::Sustain; } break;
      case Stage::Sustain: /* hold */ break;
      case Stage::Release: env_ -= (dt / std::max(1e-3f, relMs)) * (env_); if (env_ <= 0.0f) { env_ = 0.0f; envStage_ = Stage::Idle; } break;
    }
  }

  float generateWave(int wave, float pulseWidth) {
    // Simple band-limited-ish: fall back to naive plus tiny smoothing for MVP
    float x = phase_;
    switch (wave) {
      case 0: return (x < pulseWidth ? 1.0f : -1.0f); // square/pulse (placeholder)
      case 1: return 2.0f * std::fabs(2.0f * (x - std::floor(x + 0.5f))) - 1.0f; // triangle-ish (naive)
      case 2: return 2.0f * x - 1.0f; // saw (naive)
      default: return 0.0f;
    }
  }

  double sr_ = 48000.0;
  ParameterRegistry<32> params_;
  float phase_ = 0.0f;
  float env_ = 0.0f, envT_ = 0.0f;
  Stage envStage_ = Stage::Idle;

  void initParams() {
    // Initialize known params with defaults from kMamChipParamMap
    for (size_t i = 0; i < kMamChipParamMap.count; ++i) {
      const auto& d = kMamChipParamMap.defs[i];
      params_.ensureParam(d.id, d.defaultValue);
      // Map smoothing string to this registry's enum
      ParameterRegistry<32>::Smoothing s = ParameterRegistry<32>::Smoothing::Linear;
      if (std::string(d.smoothing) == std::string("step")) s = ParameterRegistry<32>::Smoothing::Step;
      else if (std::string(d.smoothing) == std::string("expo")) s = ParameterRegistry<32>::Smoothing::Expo;
      params_.setSmoothing(d.id, s);
    }
  }
};


