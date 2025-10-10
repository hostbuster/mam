#pragma once

#include <cmath>
#include <cstdint>

struct KickParams {
  float startFreqHz = 100.0f;
  float endFreqHz = 40.0f;
  float pitchDecayMs = 60.0f;
  float ampDecayMs = 200.0f;
  float gain = 0.9f;
  float bpm = 0.0f;
  float durationSec = 1.2f;
  float click = 0.0f;
  bool loop = false;
};

class KickSynth {
public:
  explicit KickSynth(const KickParams& params, double sampleRate)
  : params_(params), sampleRate_(sampleRate) {}

  void setSampleRate(double sr) { sampleRate_ = sr; }
  double sampleRate() const { return sampleRate_; }

  void reset() {
    phase_ = 0.0;
    tSec_ = 0.0;
    active_ = false;
    triggeredOnce_ = false;
    framesUntilNextTrigger_ = 0;
  }

  inline void trigger() {
    phase_ = 0.0;
    tSec_ = 0.0;
    active_ = true;
  }

  inline float process() {
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

  const KickParams& params() const { return params_; }
  KickParams& params() { return params_; }

private:
  KickParams params_{};
  double sampleRate_ = 48000.0;
  double phase_ = 0.0;
  double tSec_ = 0.0;
  uint64_t framesUntilNextTrigger_ = 0;
  bool active_ = false;
  bool triggeredOnce_ = false;
};


