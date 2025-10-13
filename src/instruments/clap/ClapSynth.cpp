#include "ClapSynth.hpp"
#include <cmath>

ClapSynth::ClapSynth(const ClapParams& params, double sampleRate)
: params_(params), sampleRate_(sampleRate) {}

void ClapSynth::setSampleRate(double sr) { sampleRate_ = sr; }
double ClapSynth::sampleRate() const { return sampleRate_; }

void ClapSynth::reset() {
  tSec_ = 0.0;
  active_ = false;
  triggeredOnce_ = false;
  framesUntilNextTrigger_ = 0;
  rngState_ = 0x12345678u;
}

void ClapSynth::trigger() {
  tSec_ = 0.0;
  active_ = true;
}

void ClapSynth::trigger(float velocity) {
  if (velocity < 0.0f) velocity = 0.0f; else if (velocity > 1.0f) velocity = 1.0f;
  velocity_ = velocity;
  trigger();
}

inline float ClapSynth::nextNoise() {
  // xorshift32
  uint32_t x = rngState_;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  rngState_ = x;
  // map to [-1,1]
  return ((x >> 9) * (1.0f / 8388607.5f)) - 1.0f;
}

float ClapSynth::process() {
  const double sr = sampleRate_;
  if (params_.loop) {
    if (framesUntilNextTrigger_ == 0) {
      trigger();
      const double secPerBeat = 60.0 / static_cast<double>(params_.bpm);
      framesUntilNextTrigger_ = static_cast<uint64_t>(secPerBeat * sr + 0.5);
    } else {
      framesUntilNextTrigger_--;
    }
  }

  float sample = 0.0f;
  if (active_) {
    const float tauAmp = params_.ampDecayMs * 0.001f;
    const float env = std::exp(static_cast<float>(-tSec_) / tauAmp);
    const float n = nextNoise();
    sample = env * n * params_.gain * velocity_;

    tSec_ += 1.0 / sr;
    if (env < 0.00005f) {
      active_ = false;
    }
  }

  return sample;
}

const ClapParams& ClapSynth::params() const { return params_; }
ClapParams& ClapSynth::params() { return params_; }



