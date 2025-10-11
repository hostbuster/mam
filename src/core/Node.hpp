#pragma once

#include <cstdint>
#include <string>
#include "Command.hpp"

struct ProcessContext {
  double sampleRate = 48000.0;
  uint32_t frames = 0;
  SampleTime blockStart = 0; // absolute sample start of this block
};

class Node {
public:
  virtual ~Node() = default;
  virtual const char* name() const = 0;
  virtual void prepare(double sampleRate, uint32_t maxBlock) = 0;
  virtual void reset() = 0;
  virtual void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) = 0;
  // Optional insert-style processing (for effects) defaults to no-op
  virtual void processInPlace(ProcessContext ctx, float* interleaved, uint32_t channels) { (void)ctx; (void)interleaved; (void)channels; }
  // Optional: handle control events prior to processing a block (currently block-accurate)
  virtual void handleEvent(const Command&) {}
};

// Observability helpers (MVP): compute peak/RMS of a buffer segment.
inline void measurePeakRms(const float* interleaved, uint32_t frames, uint32_t channels, double& outPeak, double& outRms) {
  double peak = 0.0; long double sumSq = 0.0L;
  const size_t n = static_cast<size_t>(frames) * channels;
  for (size_t i = 0; i < n; ++i) { const double s = interleaved[i]; const double a = std::fabs(s); if (a > peak) peak = a; sumSq += s * s; }
  outPeak = peak; outRms = (n > 0) ? std::sqrt(static_cast<double>(sumSq / static_cast<long double>(n))) : 0.0;
}



