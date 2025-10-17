#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include "../core/Graph.hpp"
#include "BufferPool.hpp"
#include "OfflineProgress.hpp"
#include "../core/GraphConfig.hpp"
#include "../core/Command.hpp"
#include <cmath>
#include "../core/DelayNode.hpp"
#include "../core/CompressorNode.hpp"
#include "../core/MeterNode.hpp"

// Scaffold: A minimal topological scheduler for offline rendering.
// Current Graph has no explicit edges; this placeholder processes nodes then applies mixer.
class OfflineTopoScheduler {
public:
  explicit OfflineTopoScheduler(uint32_t channels) : pool_(channels), channels_(channels) {}

  void setChannels(uint32_t channels) { channels_ = channels; pool_.setChannels(channels); }
  void setDebug(bool on) { debug_ = on; }
  void setBlockSize(uint32_t bs) { blockSize_ = (bs >= 64u) ? bs : 64u; }

  // Render 'frames' samples into interleaved output, using fixed blockSize.
  void render(Graph& graph,
              const std::vector<GraphSpec::Connection>& connections,
              const std::vector<GraphSpec::CommandSpec>& cmds,
              uint32_t sampleRate,
              uint32_t channels,
              uint64_t frames,
              std::vector<float>& out) {
    const uint32_t blockSize = blockSize_;
    graph.prepare(sampleRate, blockSize);
    graph.reset();
    out.assign(static_cast<size_t>(frames * channels), 0.0f);

    // Timeline-like segmented processing with command delivery, using graph.process for mixing/topology.
    std::vector<GraphSpec::CommandSpec> commands = cmds;
    std::sort(commands.begin(), commands.end(), [](const auto& a, const auto& b){ return a.sampleTime < b.sampleTime; });

    const auto tStart = std::chrono::steady_clock::now();
    // Ensure deterministic edge order before any processing by sorting connections
    if (!stableConnsApplied_) {
      std::vector<GraphSpec::Connection> sorted = connections;
      std::sort(sorted.begin(), sorted.end(), [](const GraphSpec::Connection& a, const GraphSpec::Connection& b){
        if (a.to != b.to) return a.to < b.to;
        if (a.from != b.from) return a.from < b.from;
        if (a.toPort != b.toPort) return a.toPort < b.toPort;
        return a.fromPort < b.fromPort;
      });
      graph.setConnections(sorted);
      stableConnsApplied_ = true;
    }
    // Build topo levels once for diagnostics (serial execution for now)
    if (!levelsBuilt_) {
      buildLevels(graph, connections);
      if (debug_) printLevels(graph);
      levelsBuilt_ = true;
    }
    size_t cmdIndex = 0;
    // Prepare ID→index for quick lookups
    idToIdx_.clear(); ids_.clear(); graph.forEachNode([&](const std::string& id, Node&){ idToIdx_[id] = ids_.size(); ids_.push_back(id); });

    // Copy sorted connections for dry taps and isSink computation
    if (stableConns_.empty()) { stableConns_ = connections; }
    // Compute sink flags from connections (nodes with no outgoing edges)
    isSink_.assign(graph.nodeCount(), true);
    for (const auto& e : stableConns_) {
      auto itF = idToIdx_.find(e.from);
      if (itF != idToIdx_.end()) isSink_[itF->second] = false;
    }

    for (uint64_t f = 0; f < frames; f += blockSize) {
      const uint32_t thisBlock = static_cast<uint32_t>(std::min<uint64_t>(blockSize, frames - f));
      const uint64_t blockStart = f;
      const uint64_t cutoff = f + thisBlock;

      std::vector<uint32_t> splits; splits.reserve(8);
      splits.push_back(0);
      size_t idx = cmdIndex;
      while (idx < commands.size() && commands[idx].sampleTime < cutoff) {
        if (commands[idx].sampleTime >= blockStart) splits.push_back(static_cast<uint32_t>(commands[idx].sampleTime - blockStart));
        ++idx;
      }
      splits.push_back(thisBlock);
      std::sort(splits.begin(), splits.end());
      splits.erase(std::unique(splits.begin(), splits.end()), splits.end());

      for (size_t si = 0; si + 1 < splits.size(); ++si) {
        const uint32_t segStart = splits[si];
        const uint32_t segEnd   = splits[si + 1];
        const uint32_t segFrames = segEnd - segStart;
        if (segFrames == 0) continue;
        const uint64_t segAbs = blockStart + segStart;

        size_t di = cmdIndex;
        while (di < commands.size() && commands[di].sampleTime < segAbs) ++di;
        // SetParam / SetParamRamp first
        for (size_t dj = di; dj < commands.size() && commands[dj].sampleTime == segAbs; ++dj) {
          if (commands[dj].type != std::string("SetParam") && commands[dj].type != std::string("SetParamRamp")) continue;
          Command c{}; c.sampleTime = segAbs; c.nodeId = commands[dj].nodeId.c_str();
          c.type = (commands[dj].type == std::string("SetParam")) ? CommandType::SetParam : CommandType::SetParamRamp;
          c.paramId = commands[dj].paramId; c.value = commands[dj].value; c.rampMs = commands[dj].rampMs;
          graph.forEachNode([&](const std::string& id, Node& n){ if (c.nodeId && id == c.nodeId) n.handleEvent(c); });
        }
        // Triggers
        for (size_t dj = di; dj < commands.size() && commands[dj].sampleTime == segAbs; ++dj) {
          if (commands[dj].type != std::string("Trigger")) continue;
          Command c{}; c.sampleTime = segAbs; c.nodeId = commands[dj].nodeId.c_str(); c.type = CommandType::Trigger; c.paramId = 0; c.value = commands[dj].value; c.rampMs = 0.0f;
          graph.forEachNode([&](const std::string& id, Node& n){ if (c.nodeId && id == c.nodeId) n.handleEvent(c); });
        }

        // Execute by topo levels with explicit per-edge accumulation and BufferPool reuse
        graph.ensureTopology();
        const size_t totalSamples = static_cast<size_t>(segFrames) * channels;
        if (nodeBuffers_.size() != graph.nodeCount()) nodeBuffers_.assign(graph.nodeCount(), static_cast<float*>(nullptr));
        // Acquire/zero output buffers for all nodes for this segment
        for (size_t i = 0; i < graph.nodeCount(); ++i) {
          auto& buf = pool_.acquire(segFrames);
          nodeBuffers_[i] = buf.data();
          std::fill(nodeBuffers_[i], nodeBuffers_[i] + totalSamples, 0.0f);
        }
        // Per-port sums for current node
        std::unordered_map<uint32_t, std::vector<float>> portSums;

        // Iterate levels
        for (const auto& level : levels_) {
          for (size_t ni : level) {
            // Build per-port sums from upstream edges
            portSums.clear();
            std::vector<Graph::EdgeInfo> ups; graph.getUpstreamEdgeInfos(ni, ups);
            for (const auto& e : ups) {
              const float* src = nodeBuffers_[e.fromIndex];
              auto& dst = portSums[e.toPort]; if (dst.size() != totalSamples) dst.assign(totalSamples, 0.0f);
              const uint32_t srcDecl = graph.getDeclaredOutChannels(e.fromIndex, e.fromPort);
              const uint32_t dstDecl = graph.getDeclaredInChannels(ni, e.toPort);
              graph.accumulateEdge(src, dst, segFrames, channels, srcDecl, dstDecl, e.gain);
            }
            // Determine main input (port 0)
            const float* mainIn = nullptr; if (portSums.count(0u)) mainIn = portSums[0u].data();
            // Process node
            Node* node = graph.nodeAt(ni);
            if (auto* d = dynamic_cast<DelayNode*>(node)) {
              // in-place over input
              if (mainIn) std::copy(portSums[0u].begin(), portSums[0u].end(), nodeBuffers_[ni]);
              ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = segFrames; ctx.blockStart = segAbs;
              d->processInPlace(ctx, nodeBuffers_[ni], channels);
            } else if (auto* c = dynamic_cast<CompressorNode*>(node)) {
              if (mainIn) std::copy(portSums[0u].begin(), portSums[0u].end(), nodeBuffers_[ni]);
              std::vector<float> sc; sc.assign(totalSamples, 0.0f);
              auto itSC = portSums.find(1u); if (itSC != portSums.end()) sc = itSC->second;
              ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = segFrames; ctx.blockStart = segAbs;
              c->applySidechain(ctx, nodeBuffers_[ni], sc.data(), channels);
            } else if (auto* m = dynamic_cast<MeterNode*>(node)) {
              if (mainIn) std::copy(portSums[0u].begin(), portSums[0u].end(), nodeBuffers_[ni]);
              ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = segFrames; ctx.blockStart = segAbs;
              m->updateFromBuffer(nodeBuffers_[ni], segFrames, channels);
            } else {
              // Generators or nodes that ignore inputs
              ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = segFrames; ctx.blockStart = segAbs;
              node->process(ctx, nodeBuffers_[ni], channels);
            }
          }
        }

        // Mix to output with dry taps and mixer gains; stable connection order
        float* outPtr = out.data() + static_cast<size_t>((blockStart + segStart) * channels);
        std::fill(outPtr, outPtr + totalSamples, 0.0f);
        for (const auto& e : stableConns_) {
          // dry tap suppression if present in mixer
          const size_t fromIdx = idToIdx_.count(e.from) ? idToIdx_[e.from] : static_cast<size_t>(-1);
          if (fromIdx == static_cast<size_t>(-1)) continue;
          const float dry = e.dryPercent * (1.0f/100.0f);
          if (dry <= 0.0f) continue;
          if (graph.mixerGainForId(e.from) > 0.0f) continue;
          const float* src = nodeBuffers_[fromIdx];
          for (size_t i = 0; i < totalSamples; ++i) outPtr[i] += src[i] * dry;
        }
        for (size_t mi = 0; mi < graph.nodeCount(); ++mi) {
          float gain = graph.mixerGainForId(graph.nodeIdAt(mi));
          if (gain == 0.0f && (isSink_.size()==graph.nodeCount() ? isSink_[mi] : true)) gain = 1.0f;
          if (gain == 0.0f) continue;
          const float* src = nodeBuffers_[mi];
          for (size_t i = 0; i < totalSamples; ++i) outPtr[i] += src[i] * gain;
        }
        if (graph.hasMixer()) {
          const float master = graph.mixerMasterGain();
          if (master != 1.0f) for (size_t i=0;i<totalSamples;++i) outPtr[i] *= master;
          if (graph.mixerSoftClipEnabled()) {
            for (size_t i=0;i<totalSamples;++i) outPtr[i] = std::tanh(outPtr[i]);
          }
        }

        pool_.releaseAll();
      }
      while (cmdIndex < commands.size() && commands[cmdIndex].sampleTime < cutoff) ++cmdIndex;

      if (gOfflineProgressEnabled && gOfflineProgressMs > 0) {
        static auto last = tStart; const auto now = std::chrono::steady_clock::now();
        const auto msSince = std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count();
        if (msSince >= gOfflineProgressMs) {
          const double frac = static_cast<double>(std::min<uint64_t>(f + blockSize, frames)) / static_cast<double>(frames);
          std::fprintf(stderr, "[offline-topo] %3.0f%%\r", frac * 100.0);
          last = now;
        }
      }
    }
  }

private:
  void buildLevels(Graph& graph, const std::vector<GraphSpec::Connection>& connections) {
    ids_.clear(); idToIdx_.clear();
    graph.forEachNode([&](const std::string& id, Node&){ idToIdx_[id] = ids_.size(); ids_.push_back(id); });
    const size_t n = ids_.size();
    std::vector<int> indeg(n, 0);
    std::unordered_multimap<size_t,size_t> adj;
    for (const auto& e : connections) {
      auto itF = idToIdx_.find(e.from), itT = idToIdx_.find(e.to);
      if (itF == idToIdx_.end() || itT == idToIdx_.end()) continue;
      indeg[itT->second]++;
      adj.emplace(itF->second, itT->second);
    }
    // Kahn levels
    levels_.clear();
    std::vector<size_t> frontier;
    for (size_t i=0;i<n;++i) if (indeg[i]==0) frontier.push_back(i);
    std::vector<int> indegWork = indeg;
    std::unordered_set<size_t> seen;
    while (!frontier.empty()) {
      levels_.push_back(frontier);
      std::vector<size_t> next;
      for (size_t u : frontier) {
        seen.insert(u);
        auto range = adj.equal_range(u);
        for (auto it = range.first; it != range.second; ++it) {
          size_t v = it->second;
          if (--indegWork[v] == 0) next.push_back(v);
        }
      }
      // Deduplicate next
      std::sort(next.begin(), next.end()); next.erase(std::unique(next.begin(), next.end()), next.end());
      frontier = std::move(next);
    }
    // If cycle or disconnected, place remaining nodes in a final level in insertion order
    if (seen.size() < n) {
      std::vector<size_t> rem;
      rem.reserve(n - seen.size());
      for (size_t i=0;i<n;++i) if (!seen.count(i)) rem.push_back(i);
      if (!rem.empty()) levels_.push_back(std::move(rem));
    }
  }

  void printLevels(Graph& graph) const {
    (void)graph;
    std::fprintf(stderr, "[offline-topo] levels=%zu\n", levels_.size());
    for (size_t li = 0; li < levels_.size(); ++li) {
      std::fprintf(stderr, "  level %zu:", li);
      for (size_t idx : levels_[li]) {
        const std::string& id = ids_[idx];
        std::fprintf(stderr, " %s", id.c_str());
      }
      std::fprintf(stderr, "\n");
    }
  }

private:
  bool debug_ = false;
  bool levelsBuilt_ = false;
  bool stableConnsApplied_ = false;
  std::vector<std::string> ids_;
  std::unordered_map<std::string,size_t> idToIdx_;
  std::vector<std::vector<size_t>> levels_;
  BufferPool pool_;
  uint32_t channels_ = 2;
  uint32_t blockSize_ = 1024;
  std::vector<float*> nodeBuffers_{};
  std::vector<bool> isSink_{};
  std::vector<GraphSpec::Connection> stableConns_{};
};


