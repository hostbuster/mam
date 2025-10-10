#pragma once

#include <vector>
#include <cstdint>
#include "../core/Graph.hpp"

inline std::vector<float> renderGraphInterleaved(Graph& graph, uint32_t sampleRate, uint32_t channels, uint64_t frames) {
  graph.prepare(sampleRate, 1024);
  graph.reset();
  std::vector<float> out;
  out.resize(static_cast<size_t>(frames * channels));
  ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = 0;
  for (uint64_t i = 0; i < frames; ) {
    const uint32_t block = static_cast<uint32_t>(std::min<uint64_t>(1024, frames - i));
    ctx.frames = block;
    graph.process(ctx, out.data() + static_cast<size_t>(i * channels), channels);
    i += block;
  }
  return out;
}


