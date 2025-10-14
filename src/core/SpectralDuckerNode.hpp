#pragma once

#include "CompressorNode.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

// Minimal multiband spectral ducking built on CompressorNode plumbing.
// Uses 3 peaking filters as approximate bands and applies per-band envelope gain.
class SpectralDuckerNode : public CompressorNode {
public:
  struct Band { float centerHz = 100.0f; float q = 1.0f; float depthDb = -6.0f; };

  // Parameters
  float lookaheadMs = 5.0f; // reserved for future use
  float mix = 1.0f;         // 0..1
  std::vector<Band> bands{{60.0f,1.0f,-9.0f},{120.0f,1.0f,-6.0f},{250.0f,0.8f,-3.0f}};

  const char* name() const override { return "spectral_ducker"; }

  void prepare(double sampleRate, uint32_t maxBlock) override {
    CompressorNode::prepare(sampleRate, maxBlock);
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    setupBands();
  }
  void reset() override {
    CompressorNode::reset();
    for (auto& s : states_) s = BiquadState{};
    for (auto& e : envs_) e = 0.0f;
  }

  void applySidechain(ProcessContext ctx, float* mainInterleaved, const float* scInterleaved, uint32_t channels) override {
    const uint32_t frames = ctx.frames;
    if (channels == 0 || frames == 0) return;
    // Build mono sidechain
    scMono_.assign(frames, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
      double sum = 0.0;
      for (uint32_t c = 0; c < channels; ++c) sum += scInterleaved[static_cast<size_t>(i)*channels + c];
      scMono_[i] = static_cast<float>(sum / static_cast<double>(channels));
    }
    // For each band: filter detector -> envelope -> gain curve
    if (filters_.size() != bands.size()) setupBands();
    if (envs_.size() != bands.size()) envs_.assign(bands.size(), 0.0f);
    if (states_.size() != bands.size()) states_.assign(bands.size(), BiquadState{});

    gains_.assign(frames, 1.0f);
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      const auto& b = bands[bi];
      const float depthLin = std::pow(10.0f, b.depthDb / 20.0f); // < 1.0
      const Biquad& q = filters_[bi];
      BiquadState& st = states_[bi];
      float env = envs_[bi];
      for (uint32_t i = 0; i < frames; ++i) {
        float x = scMono_[i];
        float y = q.process(x, st);
        float rect = std::fabs(y);
        const float coef = (rect > env) ? attackCoef_ : releaseCoef_;
        env = rect + coef * (env - rect);
        const float k = (env >= 1e-3f) ? depthLin + (1.0f - depthLin) * (1.0f - std::min(env, 1.0f)) : 1.0f;
        gains_[i] = std::min(gains_[i], k);
      }
      envs_[bi] = env;
    }
    // Apply per-sample band-combined gain to main signal
    const float wet = std::clamp(mix, 0.0f, 1.0f);
    const float dry = 1.0f - wet;
    for (uint32_t i = 0; i < frames; ++i) {
      const float g = gains_[i] * wet + dry;
      for (uint32_t c = 0; c < channels; ++c) {
        mainInterleaved[static_cast<size_t>(i)*channels + c] *= g;
      }
    }
  }

private:
  // Simple biquad peaking EQ structure
  struct BiquadState { float z1=0,z2=0; };
  struct Biquad { float b0=1,b1=0,b2=0,a1=0,a2=0; float process(float x, BiquadState& s) const; };
  static Biquad designPeaking(float sampleRate, float centerHz, float q, float gainDb) {
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * (centerHz / sampleRate);
    const float cosw = std::cos(w0), sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * q);
    float b0 = 1 + alpha * A;
    float b1 = -2 * cosw;
    float b2 = 1 - alpha * A;
    float a0 = 1 + alpha / A;
    float a1 = -2 * cosw;
    float a2 = 1 - alpha / A;
    Biquad biq{}; biq.b0 = b0/a0; biq.b1 = b1/a0; biq.b2 = b2/a0; biq.a1 = a1/a0; biq.a2 = a2/a0; return biq;
  }
  static Biquad designBandpass(float sampleRate, float centerHz, float q) {
    const float w0 = 2.0f * static_cast<float>(M_PI) * (centerHz / sampleRate);
    const float cosw = std::cos(w0), sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * q);
    float b0 = alpha, b1 = 0, b2 = -alpha;
    float a0 = 1 + alpha; float a1 = -2 * cosw; float a2 = 1 - alpha;
    Biquad biq{}; biq.b0 = b0/a0; biq.b1 = b1/a0; biq.b2 = b2/a0; biq.a1 = a1/a0; biq.a2 = a2/a0; return biq;
  }
  void setupBands() {
    filters_.clear(); filters_.reserve(bands.size());
    for (const auto& b : bands) filters_.push_back(designBandpass(static_cast<float>(sampleRate_), b.centerHz, std::max(0.1f, b.q)));
    states_.assign(bands.size(), BiquadState{});
    envs_.assign(bands.size(), 0.0f);
  }

  double sampleRate_ = 48000.0;
  std::vector<float> scMono_{};
  std::vector<float> gains_{};
  std::vector<Biquad> filters_{};
  std::vector<BiquadState> states_{};
  std::vector<float> envs_{};
};

inline float SpectralDuckerNode::Biquad::process(float x, SpectralDuckerNode::BiquadState& s) const {
  const float y = b0*x + b1*s.z1 + b2*s.z2 - a1*s.z1 - a2*s.z2; // Direct Form I (simplified)
  s.z2 = s.z1; s.z1 = y;
  return y;
}


