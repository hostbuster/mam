#pragma once

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
  explicit KickSynth(const KickParams& params, double sampleRate);
  void setSampleRate(double sr);
  double sampleRate() const;
  void reset();
  void trigger();
  void trigger(float velocity);
  float process();
  const KickParams& params() const;
  KickParams& params();

private:
  KickParams params_{};
  double sampleRate_ = 48000.0;
  double phase_ = 0.0;
  double tSec_ = 0.0;
  uint64_t framesUntilNextTrigger_ = 0;
  bool active_ = false;
  bool triggeredOnce_ = false;
  float velocity_ = 1.0f;
};



