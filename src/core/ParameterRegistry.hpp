#pragma once

#include <cstdint>
#include <array>

// Tiny fixed-capacity parameter smoother registry for realtime use
// No dynamic allocations; linear lookup (small N)
template <size_t MaxParams = 8>
class ParameterRegistry {
public:
  void prepare(double sampleRate) { sampleRate_ = sampleRate; }

  void ensureParam(uint16_t id, float initialValue) {
    if (findIndex(id) != -1) return;
    if (size_ < MaxParams) {
      entries_[size_++] = Entry{id, initialValue, initialValue, 0.0f, 0u};
    }
  }

  void setImmediate(uint16_t id, float value) {
    const int idx = findIndex(id);
    if (idx < 0) return;
    entries_[static_cast<size_t>(idx)].current = value;
    entries_[static_cast<size_t>(idx)].target = value;
    entries_[static_cast<size_t>(idx)].deltaPerSample = 0.0f;
    entries_[static_cast<size_t>(idx)].samplesLeft = 0u;
  }

  void rampTo(uint16_t id, float target, float rampMs) {
    const int idx = findIndex(id);
    if (idx < 0) return;
    Entry& e = entries_[static_cast<size_t>(idx)];
    const uint32_t samples = rampMs <= 0.0f ? 0u : static_cast<uint32_t>((rampMs * 0.001) * sampleRate_ + 0.5);
    if (samples == 0) {
      e.current = target;
      e.target = target;
      e.deltaPerSample = 0.0f;
      e.samplesLeft = 0u;
    } else {
      e.target = target;
      e.deltaPerSample = (target - e.current) / static_cast<float>(samples);
      e.samplesLeft = samples;
    }
  }

  // Advance one sample and return the current value
  float next(uint16_t id) {
    const int idx = findIndex(id);
    if (idx < 0) return 0.0f;
    Entry& e = entries_[static_cast<size_t>(idx)];
    if (e.samplesLeft > 0) {
      e.current += e.deltaPerSample;
      e.samplesLeft--;
      if (e.samplesLeft == 0) e.current = e.target;
    }
    return e.current;
  }

  float current(uint16_t id) const {
    const int idx = findIndex(id);
    if (idx < 0) return 0.0f;
    return entries_[static_cast<size_t>(idx)].current;
  }

private:
  struct Entry {
    uint16_t id;
    float current;
    float target;
    float deltaPerSample;
    uint32_t samplesLeft;
  };

  int findIndex(uint16_t id) const {
    for (size_t i = 0; i < size_; ++i) if (entries_[i].id == id) return static_cast<int>(i);
    return -1;
  }

  std::array<Entry, MaxParams> entries_{};
  size_t size_ = 0;
  double sampleRate_ = 48000.0;
};


