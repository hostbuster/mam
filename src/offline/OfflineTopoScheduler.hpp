#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include "../core/Graph.hpp"
#include "BufferPool.hpp"

// Scaffold: A minimal topological scheduler for offline rendering.
// Current Graph has no explicit edges; this placeholder processes nodes then applies mixer.
class OfflineTopoScheduler {
public:
  explicit OfflineTopoScheduler(uint32_t channels) : pool_(channels), channels_(channels) {}

  void setChannels(uint32_t channels) { channels_ = channels; pool_.setChannels(channels); }

  // Render 'frames' samples into interleaved output, using fixed blockSize.
  void render(Graph& graph, uint32_t sampleRate, uint64_t frames, uint32_t blockSize, std::vector<float>& out) {
    graph.prepare(sampleRate, blockSize);
    graph.reset();
    out.assign(static_cast<size_t>(frames * channels_), 0.0f);

    // Snapshot node IDs and pointers for stable iteration
    std::vector<std::string> ids; ids.reserve(32);
    std::vector<Node*> nodes; nodes.reserve(32);
    graph.forEachNode([&](const std::string& id, Node& n){ ids.push_back(id); nodes.push_back(&n); });

    for (uint64_t f = 0; f < frames; f += blockSize) {
      const uint32_t thisBlock = static_cast<uint32_t>(std::min<uint64_t>(blockSize, frames - f));

      // Acquire one buffer per node
      std::vector<float*> nodeBuffers(nodes.size(), nullptr);
      for (size_t i = 0; i < nodes.size(); ++i) {
        auto& buf = pool_.acquire(thisBlock);
        nodeBuffers[i] = buf.data();
      }

      // Process nodes in insertion order (placeholder for topo order)
      for (size_t i = 0; i < nodes.size(); ++i) {
        ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = thisBlock; ctx.blockStart = f;
        nodes[i]->process(ctx, nodeBuffers[i], channels_);
      }

      // Mix down into output
      float* outPtr = out.data() + static_cast<size_t>(f * channels_);
      const size_t samples = static_cast<size_t>(thisBlock * channels_);
      std::fill(outPtr, outPtr + samples, 0.0f);
      for (size_t i = 0; i < nodes.size(); ++i) {
        const float* src = nodeBuffers[i];
        for (size_t s = 0; s < samples; ++s) outPtr[s] += src[s];
      }

      pool_.releaseAll();
    }
  }

private:
  BufferPool pool_;
  uint32_t channels_ = 2;
};


