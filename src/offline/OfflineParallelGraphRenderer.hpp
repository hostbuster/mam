#pragma once

#include <vector>
#include <thread>
#include <functional>
#include <algorithm>
#include <cstdint>
#include "../core/Graph.hpp"
#include "OfflineProgress.hpp"

inline std::vector<float> renderGraphInterleavedParallel(Graph& graph, uint32_t sampleRate, uint32_t channels, uint64_t frames, uint32_t numThreads) {
  graph.prepare(sampleRate, 1024);
  graph.reset();

  std::vector<float> out;
  out.resize(static_cast<size_t>(frames * channels), 0.0f);

  // Collect node pointers once (stable for the render duration)
  std::vector<Node*> nodes;
  nodes.reserve(32);
  graph.forEachNode([&](const std::string&, Node& n){ nodes.push_back(&n); });

  const uint32_t block = 1024;
  const auto tStart = std::chrono::steady_clock::now();
  uint64_t processed = 0;
  for (uint64_t f = 0; f < frames; f += block) {
    const uint32_t thisBlock = static_cast<uint32_t>(std::min<uint64_t>(block, frames - f));

    // Prepare per-node buffers
    std::vector<std::vector<float>> nodeBuffers(nodes.size());
    for (auto& buf : nodeBuffers) buf.assign(static_cast<size_t>(thisBlock * channels), 0.0f);

    // Parallel process each node into its buffer
    const uint32_t useThreads = std::max<uint32_t>(1, numThreads);
    if (useThreads == 1 || nodes.size() <= 1) {
      for (size_t ni = 0; ni < nodes.size(); ++ni) {
        ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = thisBlock; ctx.blockStart = f;
        nodes[ni]->process(ctx, nodeBuffers[ni].data(), channels);
      }
    } else {
      std::vector<std::thread> threads;
      threads.reserve(nodes.size());
      for (size_t ni = 0; ni < nodes.size(); ++ni) {
        threads.emplace_back([&, ni](){
          ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = thisBlock; ctx.blockStart = f;
          nodes[ni]->process(ctx, nodeBuffers[ni].data(), channels);
        });
        if (threads.size() == useThreads || ni + 1 == nodes.size()) {
          for (auto& t : threads) t.join();
          threads.clear();
        }
      }
    }

    // Mix node buffers into output
    float* outPtr = out.data() + static_cast<size_t>(f * channels);
    const size_t samples = static_cast<size_t>(thisBlock * channels);
    std::fill(outPtr, outPtr + samples, 0.0f);
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
      const float* src = nodeBuffers[ni].data();
      for (size_t i = 0; i < samples; ++i) outPtr[i] += src[i];
    }
    processed += thisBlock;
    if (gOfflineProgressEnabled && gOfflineProgressMs > 0) {
      static auto last = tStart;
      const auto now = std::chrono::steady_clock::now();
      const auto msSince = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
      if (msSince >= gOfflineProgressMs) {
        const double frac = static_cast<double>(processed) / static_cast<double>(frames);
        std::fprintf(stderr, "[offline-par] %3.0f%%\r", frac * 100.0);
        last = now;
      }
    }
  }
  const auto tEnd = std::chrono::steady_clock::now();
  const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(tEnd - tStart).count());
  const double sec = ns / 1e9;
  const double rtSec = static_cast<double>(frames) / static_cast<double>(sampleRate);
  if (gOfflineSummaryEnabled && rtSec > 0.0) std::fprintf(stderr, "[offline-par] done in %.3fs (speedup %.1fx)    \n", sec, rtSec / sec);
  return out;
}


