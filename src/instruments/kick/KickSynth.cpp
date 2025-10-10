#include "KickSynth.hpp"
#include <cmath>

KickSynth::KickSynth(const KickParams& params, double sampleRate)
: params_(params), sampleRate_(sampleRate) {}

void KickSynth::setSampleRate(double sr) { sampleRate_ = sr; }
double KickSynth::sampleRate() const { return sampleRate_; }

void KickSynth::reset() {
  phase_ = 0.0;
  tSec_ = 0.0;
  active_ = false;
  triggeredOnce_ = false;
  framesUntilNextTrigger_ = 0;
}

void KickSynth::trigger() {
  phase_ = 0.0;
  tSec_ = 0.0;
  active_ = true;
}

float KickSynth::process() {
  const double sr = sampleRate_;
  if (params_.loop) {
    if (framesUntilNextTrigger_ == 0) {
      trigger();
      const double secPerBeat = 60.0 / static_cast<double>(params_.bpm);
      framesUntilNextTrigger_ = static_cast<uint64_t>(secPerBeat * sr + 0.5);
    } else {
      framesUntilNextTrigger_--;
    }
  } else if (!triggeredOnce_) {
    trigger();
    triggeredOnce_ = true;
  }

  float sample = 0.0f;
  if (active_) {
    const float tauPitch = params_.pitchDecayMs * 0.001f;
    const float tauAmp = params_.ampDecayMs * 0.001f;
    const float aEnv = std::exp(static_cast<float>(-tSec_) / tauAmp);
    const float fEnv = std::exp(static_cast<float>(-tSec_) / tauPitch);
    const float freq = params_.endFreqHz + (params_.startFreqHz - params_.endFreqHz) * fEnv;

    phase_ += (2.0 * M_PI) * static_cast<double>(freq) / sr;
    if (phase_ > 1e12) phase_ = std::fmod(phase_, 2.0 * M_PI);

    const float s = std::sin(static_cast<float>(phase_));
    const float click = (tSec_ < (1.5 / sr)) ? params_.click : 0.0f;
    sample = (aEnv * s + click) * params_.gain;

    tSec_ += 1.0 / sr;
    if (aEnv < 0.00005f) {
      active_ = false;
    }
  }

  return sample;
}

const KickParams& KickSynth::params() const { return params_; }
KickParams& KickSynth::params() { return params_; }



