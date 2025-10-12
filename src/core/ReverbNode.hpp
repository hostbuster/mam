#pragma once

#include "Node.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

// Simple stereo Schroeder-style reverb (very lightweight, demo-quality)
class ReverbNode : public Node {
public:
  float roomSize = 0.5f;   // 0..1
  float damp = 0.3f;       // 0..1
  float mix = 0.2f;        // 0..1

  const char* name() const override { return "reverb"; }

  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    initDelayLines();
  }

  void reset() override {
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    idxL_ = idxR_ = 0;
    lpL_ = lpR_ = 0.0f;
  }

  void process(ProcessContext, float*, uint32_t) override {}

  void processInPlace(ProcessContext ctx, float* interleaved, uint32_t channels) override {
    if (channels != 2) return; // stereo only for MVP
    const uint32_t n = ctx.frames;
    const float gWet = mix;
    const float gDry = 1.0f - mix;
    const float combFeedback = 0.75f + 0.2f * roomSize; // 0.75..0.95
    const float dampCoef = std::clamp(damp, 0.0f, 0.99f);

    for (uint32_t i = 0; i < n; ++i) {
      const size_t pL = idxL_ % delayL_.size();
      const size_t pR = idxR_ % delayR_.size();

      float inL = interleaved[2*i];
      float inR = interleaved[2*i + 1];

      // Lowpass in feedback path
      lpL_ = (1.0f - dampCoef) * delayL_[pL] + dampCoef * lpL_;
      lpR_ = (1.0f - dampCoef) * delayR_[pR] + dampCoef * lpR_;

      // Comb-like feedback with input
      float fbL = lpL_ * combFeedback + inL * 0.2f;
      float fbR = lpR_ * combFeedback + inR * 0.2f;

      // Write back
      delayL_[pL] = fbL;
      delayR_[pR] = fbR;

      // Simple allpass-ish diffusion using tapped samples
      float yL = tapMix(delayL_, pL);
      float yR = tapMix(delayR_, pR);

      interleaved[2*i]     = gDry * inL + gWet * yL;
      interleaved[2*i + 1] = gDry * inR + gWet * yR;

      idxL_++; idxR_++;
    }
  }

private:
  void initDelayLines() {
    // Delay lengths as primes near ~30..80ms at 48k
    const int base = static_cast<int>(sampleRate_ * 0.03);
    const int lenL = nextPrime(base + 389);
    const int lenR = nextPrime(base + 433);
    delayL_.assign(static_cast<size_t>(lenL), 0.0f);
    delayR_.assign(static_cast<size_t>(lenR), 0.0f);
    idxL_ = idxR_ = 0; lpL_ = lpR_ = 0.0f;
  }

  static int nextPrime(int n) {
    auto isPrime = [](int x){ if (x<2) return false; for (int i=2;i*i<=x;++i) if (x%i==0) return false; return true; };
    while (!isPrime(n)) ++n; return n;
  }

  static float tapMix(const std::vector<float>& buf, size_t pos) {
    const size_t N = buf.size();
    auto at = [&](size_t off){ return buf[(pos + N - (off % N)) % N]; };
    // 4 taps with small weights for diffusion
    return 0.4f * at(0) + 0.3f * at(113) + 0.2f * at(263) + 0.1f * at(397);
  }

  double sampleRate_ = 48000.0;
  std::vector<float> delayL_;
  std::vector<float> delayR_;
  size_t idxL_ = 0, idxR_ = 0;
  float lpL_ = 0.0f, lpR_ = 0.0f;
};


