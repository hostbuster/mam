#pragma once

#include <cstdint>

struct ClapParams {
  float ampDecayMs = 180.0f;
  float gain = 0.9f;
  float bpm = 0.0f;   // if > 0, loop at BPM
  bool loop = false;
};

class ClapSynth {
public:
  explicit ClapSynth(const ClapParams& params, double sampleRate);
  void setSampleRate(double sr);
  double sampleRate() const;
  void reset();
  void trigger();
  float process();
  const ClapParams& params() const;
  ClapParams& params();

private:
  inline float nextNoise();

  ClapParams params_{};
  double sampleRate_ = 48000.0;
  double tSec_ = 0.0;
  uint64_t framesUntilNextTrigger_ = 0;
  bool active_ = false;
  bool triggeredOnce_ = false;
  uint32_t rngState_ = 0x12345678u;
};



