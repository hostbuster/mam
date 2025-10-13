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

  // Dynamic per-sample frequency modulation; updates phase increment immediately
  void setDynamicFreqHz(float freqHz) {
    freqHz_ = freqHz;
    updatePhaseInc();
  }

  void setDynamicPhase01(float phase01) {
    phase_ = wrap01(phase01);
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

template <size_t MaxSources = 4, size_t MaxRoutes = 16>
class ModMatrix {
public:
  void prepare(double sampleRate) {
    for (size_t i = 0; i < numSources_; ++i) {
      sources_[i].lfo.prepare(sampleRate);
      sources_[i].smoothedFreqHz = sources_[i].baseFreqHz;
      // Slew to reach ~95% in ~5 ms
      const float tauSamples = static_cast<float>((0.005 * sampleRate) / 3.0);
      sources_[i].freqSlewAlpha = tauSamples <= 1.0f ? 1.0f : (1.0f - std::exp(-1.0f / tauSamples));
    }
  }

  struct Source {
    uint16_t id = 0; // user-visible source id
    ModLfo lfo{};
    float last = 0.0f; // cached output (bipolar)
    bool active = false;
    float baseFreqHz = 0.5f; // for LFO freq routing
    float smoothedFreqHz = 0.5f;
    float freqSlewAlpha = 1.0f;
  };

  struct Route {
    enum class Target : uint8_t { DestParam = 0, LfoFreq = 1, LfoPhase = 2 };
    Target target = Target::DestParam;
    uint16_t sourceId = 0;
    uint16_t destParamId = 0; // when Target::DestParam
    uint16_t lfoTargetId = 0; // when Target::LfoFreq
    uint16_t lfoPhaseTargetId = 0; // when Target::LfoPhase
    float depth = 0.0f;   // scales bipolar source
    float offset = 0.0f;  // constant additive offset
    bool active = false;
    // Optional mapping range and curve
    bool hasRange = false;
    float minValue = 0.0f;
    float maxValue = 0.0f;
    enum class Map : uint8_t { Linear = 0, Exp = 1 };
    Map map = Map::Linear;
  };

  bool addLfo(uint16_t id, ModLfo::Wave wave, float freqHz, float phase01) {
    const int idx = findSourceIndex(id);
    if (idx >= 0) {
      sources_[static_cast<size_t>(idx)].lfo.set(wave, freqHz, phase01);
      sources_[static_cast<size_t>(idx)].active = true;
      sources_[static_cast<size_t>(idx)].baseFreqHz = freqHz;
      return true;
    }
    if (numSources_ >= MaxSources) return false;
    Source s{};
    s.id = id;
    s.lfo.set(wave, freqHz, phase01);
    s.active = true;
    s.baseFreqHz = freqHz;
    sources_[numSources_++] = s;
    return true;
  }

  bool addRoute(uint16_t sourceId, uint16_t destParamId, float depth, float offset = 0.0f) {
    if (numRoutes_ >= MaxRoutes) return false;
    Route r{};
    r.target = Route::Target::DestParam;
    r.sourceId = sourceId;
    r.destParamId = destParamId;
    r.depth = depth;
    r.offset = offset;
    r.active = true;
    routes_[numRoutes_++] = r;
    return true;
  }

  bool addRouteWithRange(uint16_t sourceId, uint16_t destParamId, float minValue, float maxValue, typename Route::Map map = Route::Map::Linear) {
    if (numRoutes_ >= MaxRoutes) return false;
    Route r{};
    r.target = Route::Target::DestParam;
    r.sourceId = sourceId;
    r.destParamId = destParamId;
    r.active = true;
    r.hasRange = true;
    r.minValue = minValue;
    r.maxValue = maxValue;
    r.map = map;
    routes_[numRoutes_++] = r;
    return true;
  }

  bool addLfoFreqRoute(uint16_t sourceId, uint16_t lfoId, float depth, float offset = 0.0f) {
    if (numRoutes_ >= MaxRoutes) return false;
    Route r{};
    r.target = Route::Target::LfoFreq;
    r.sourceId = sourceId;
    r.lfoTargetId = lfoId;
    r.depth = depth;
    r.offset = offset;
    r.active = true;
    routes_[numRoutes_++] = r;
    return true;
  }

  bool addLfoPhaseRoute(uint16_t sourceId, uint16_t lfoId, float depth, float offset = 0.0f) {
    if (numRoutes_ >= MaxRoutes) return false;
    Route r{};
    r.target = Route::Target::LfoPhase;
    r.sourceId = sourceId;
    r.lfoPhaseTargetId = lfoId;
    r.depth = depth;
    r.offset = offset;
    r.active = true;
    routes_[numRoutes_++] = r;
    return true;
  }

  // Advance all sources one sample and cache outputs
  void tick() {
    // 1) Compute per-LFO dynamic frequency and/or phase from routes
    for (size_t i = 0; i < numSources_; ++i) {
      if (!sources_[i].active) continue;
      float freqMod = 0.0f;
      bool havePhase = false;
      float phaseVal = 0.0f;
      for (size_t r = 0; r < numRoutes_; ++r) {
        const Route& route = routes_[r];
        if (!route.active) continue;
        if (route.target == Route::Target::LfoFreq && route.lfoTargetId == sources_[i].id) {
          const int sIdx = findSourceIndex(route.sourceId);
          if (sIdx < 0) continue;
          const Source& src = sources_[static_cast<size_t>(sIdx)];
          freqMod += route.offset + route.depth * src.last;
        } else if (route.target == Route::Target::LfoPhase && route.lfoPhaseTargetId == sources_[i].id) {
          const int sIdx = findSourceIndex(route.sourceId);
          if (sIdx < 0) continue;
          const Source& src = sources_[static_cast<size_t>(sIdx)];
          float t;
          if (route.hasRange) {
            t = 0.5f * (src.last + 1.0f);
            if (route.map == Route::Map::Exp) t = t * t;
            t = route.minValue + (route.maxValue - route.minValue) * t; // absolute phase in [min,max]
          } else {
            t = route.offset + route.depth * src.last; // absolute phase fraction
          }
          // If multiple phase routes target the same LFO, last one wins (authoring should avoid conflicts)
          phaseVal = t;
          havePhase = true;
        }
      }
      float desiredHz = sources_[i].baseFreqHz + freqMod;
      if (desiredHz < 0.01f) desiredHz = 0.01f;
      // one-pole slew on frequency
      sources_[i].smoothedFreqHz += sources_[i].freqSlewAlpha * (desiredHz - sources_[i].smoothedFreqHz);
      sources_[i].lfo.setDynamicFreqHz(sources_[i].smoothedFreqHz);
      if (havePhase) {
        // clamp to [0,1]
        if (phaseVal < 0.0f) phaseVal = 0.0f; else if (phaseVal > 1.0f) phaseVal = 1.0f;
        sources_[i].lfo.setDynamicPhase01(phaseVal);
      }
    }
    // 2) Advance sources and cache outputs for use by param routes and next tick
    for (size_t i = 0; i < numSources_; ++i) {
      if (sources_[i].active) sources_[i].last = sources_[i].lfo.next();
    }
  }

  // Sum contributions for a destination parameter id
  float sumFor(uint16_t destParamId) const {
    float acc = 0.0f;
    for (size_t i = 0; i < numRoutes_; ++i) {
      const Route& r = routes_[i];
      if (!r.active || r.target != Route::Target::DestParam || r.destParamId != destParamId) continue;
      const int sIdx = findSourceIndex(r.sourceId);
      if (sIdx < 0) continue;
      const Source& s = sources_[static_cast<size_t>(sIdx)];
      if (r.hasRange) {
        // Map [-1,1] to [min,max] with optional exponential curve
        float t = 0.5f * (s.last + 1.0f); // 0..1
        if (r.map == Route::Map::Exp) {
          // simple exponential-ish curve
          t = t * t;
        }
        acc += r.minValue + (r.maxValue - r.minValue) * t;
      } else {
        acc += r.offset + r.depth * s.last;
      }
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


