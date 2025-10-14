#pragma once

#include <vector>
#include <cstdint>
#include "../core/Graph.hpp"
#include "OfflineProgress.hpp"

inline std::vector<float> renderGraphInterleaved(Graph& graph, uint32_t sampleRate, uint32_t channels, uint64_t frames) {
  graph.prepare(sampleRate, 1024);
  graph.reset();
  std::vector<float> out;
  out.resize(static_cast<size_t>(frames * channels));
  ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = 0;
  uint64_t processed = 0; (void)processed;
  const auto tStart = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < frames; ) {
    const uint32_t block = static_cast<uint32_t>(std::min<uint64_t>(1024, frames - i));
    ctx.frames = block;
    graph.process(ctx, out.data() + static_cast<size_t>(i * channels), channels);
    i += block; processed += block;
    if (gOfflineProgressEnabled && gOfflineProgressMs > 0) {
      const auto now = std::chrono::steady_clock::now();
      static auto last = tStart;
      const auto msSince = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
      if (msSince >= gOfflineProgressMs) {
        const double frac = static_cast<double>(i) / static_cast<double>(frames);
        std::fprintf(stderr, "[offline] %3.0f%%\r", frac * 100.0);
        last = now;
      }
    }
  }
  const auto tEnd = std::chrono::steady_clock::now();
  const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count());
  const double sec = ns / 1e9;
  const double rtSec = static_cast<double>(frames) / static_cast<double>(sampleRate);
  if (gOfflineSummaryEnabled && rtSec > 0.0) std::fprintf(stderr, "[offline] done in %.3fs (speedup %.1fx)    \n", sec, rtSec / sec);
  return out;
}


