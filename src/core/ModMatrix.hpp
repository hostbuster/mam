#pragma once

#include <cstdint>
#include <array>
#include <cmath>

// Lightweight, fixed-capacity modulation matrix suitable for realtime use.
// - No dynamic allocations
// - Tick per sample, then query per-destination accumulated modulation
// - Sources are simple LFOs for now; can be extended with envelopes or external inputs

class ModLfo {
public:
  enum class Wave : uint8_t { Sine = 0, Triangle = 1, Saw = 2, Square = 3 };

  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    phase_ = wrap01(phase_);
    updatePhaseInc();
  }

  void set(Wave wave, float freqHz, float phase01) {
    wave_ = wave;
    freqHz_ = freqHz;
    phase_ = wrap01(phase01);
    updatePhaseInc();
  }

  // Returns bipolar [-1, +1]
  float next() {
    float out = 0.0f;
    switch (wave_) {
      case Wave::Sine: out = std::sin(2.0f * static_cast<float>(M_PI) * phase_); break;
      case Wave::Triangle: out = 4.0f * std::fabs(phase_ - 0.5f) - 1.0f; break;
      case Wave::Saw: out = 2.0f * phase_ - 1.0f; break;
      case Wave::Square: out = (phase_ < 0.5f) ? 1.0f : -1.0f; break;
    }
    phase_ += phaseInc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return out;
  }

private:
  static float wrap01(float x) { while (x < 0.0f) x += 1.0f; while (x >= 1.0f) x -= 1.0f; return x; }
  void updatePhaseInc() {
    const float sr = static_cast<float>(sampleRate_ > 0.0 ? sampleRate_ : 48000.0);
    const float f = freqHz_ < 0.0f ? 0.0f : freqHz_;
    phaseInc_ = f / sr;
  }

  Wave wave_ = Wave::Sine;
  float freqHz_ = 0.5f;
  float phase_ = 0.0f;    // 0..1
  float phaseInc_ = 0.0f; // per-sample
  double sampleRate_ = 48000.0;
};

template <size_t MaxSources = 4, size_t MaxRoutes = 8>
class ModMatrix {
public:
  void prepare(double sampleRate) {
    for (size_t i = 0; i < numSources_; ++i) sources_[i].lfo.prepare(sampleRate);
  }

  struct Source {
    uint16_t id = 0; // user-visible source id
    ModLfo lfo{};
    float last = 0.0f; // cached output (bipolar)
    bool active = false;
  };

  struct Route {
    uint16_t sourceId = 0;
    uint16_t destParamId = 0;
    float depth = 0.0f;   // scales bipolar source
    float offset = 0.0f;  // constant additive offset
    bool active = false;
  };

  bool addLfo(uint16_t id, ModLfo::Wave wave, float freqHz, float phase01) {
    const int idx = findSourceIndex(id);
    if (idx >= 0) {
      sources_[static_cast<size_t>(idx)].lfo.set(wave, freqHz, phase01);
      sources_[static_cast<size_t>(idx)].active = true;
      return true;
    }
    if (numSources_ >= MaxSources) return false;
    Source s{};
    s.id = id;
    s.lfo.set(wave, freqHz, phase01);
    s.active = true;
    sources_[numSources_++] = s;
    return true;
  }

  bool addRoute(uint16_t sourceId, uint16_t destParamId, float depth, float offset = 0.0f) {
    if (numRoutes_ >= MaxRoutes) return false;
    Route r{};
    r.sourceId = sourceId;
    r.destParamId = destParamId;
    r.depth = depth;
    r.offset = offset;
    r.active = true;
    routes_[numRoutes_++] = r;
    return true;
  }

  // Advance all sources one sample and cache outputs
  void tick() {
    for (size_t i = 0; i < numSources_; ++i) {
      if (sources_[i].active) sources_[i].last = sources_[i].lfo.next();
    }
  }

  // Sum contributions for a destination parameter id
  float sumFor(uint16_t destParamId) const {
    float acc = 0.0f;
    for (size_t i = 0; i < numRoutes_; ++i) {
      const Route& r = routes_[i];
      if (!r.active || r.destParamId != destParamId) continue;
      const int sIdx = findSourceIndex(r.sourceId);
      if (sIdx < 0) continue;
      const Source& s = sources_[static_cast<size_t>(sIdx)];
      acc += r.offset + r.depth * s.last;
    }
    return acc;
  }

private:
  int findSourceIndex(uint16_t id) const {
    for (size_t i = 0; i < numSources_; ++i) if (sources_[i].id == id) return static_cast<int>(i);
    return -1;
  }

  std::array<Source, MaxSources> sources_{};
  std::array<Route, MaxRoutes> routes_{};
  size_t numSources_ = 0;
  size_t numRoutes_ = 0;
};


