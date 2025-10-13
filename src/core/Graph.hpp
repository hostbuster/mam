#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <string>
#include "Node.hpp"
#include "MixerNode.hpp"
#include "DelayNode.hpp"
#include "MeterNode.hpp"
#include "CompressorNode.hpp"
#include "GraphConfig.hpp"
#include <unordered_map>
#include <unordered_set>

class Graph {
public:
  void addNode(std::string id, std::unique_ptr<Node> node) {
    nodes_.push_back(NodeEntry{std::move(id), std::move(node)});
    topoDirty_ = true;
  }
  void setMixer(std::unique_ptr<MixerNode> mixer) { mixer_ = std::move(mixer); }
  void setConnections(const std::vector<GraphSpec::Connection>& conns) {
    connections_ = conns;
    topoDirty_ = true;
  }
  // Optional: provide per-node port descriptors parsed from JSON for validation/adaptation
  void setPortDescriptors(const std::vector<NodeSpec>& nodeSpecs) {
    inPortChannels_.clear(); outPortChannels_.clear(); idToIndex_.clear();
    for (size_t i = 0; i < nodes_.size(); ++i) idToIndex_[nodes_[i].id] = i;
    for (const auto& ns : nodeSpecs) {
      auto it = idToIndex_.find(ns.id);
      if (it == idToIndex_.end()) continue;
      const size_t idx = it->second;
      for (const auto& ip : ns.ports.inputs) inPortChannels_[idx][ip.index] = ip.channels;
      for (const auto& op : ns.ports.outputs) outPortChannels_[idx][op.index] = op.channels;
    }
  }

  // Iterate nodes with their ids (read-only access to Node&). Not realtime-safe to mutate graph.
  void forEachNode(const std::function<void(const std::string&, Node&)>& fn) {
    for (auto& e : nodes_) fn(e.id, *e.node);
  }

  void prepare(double sampleRate, uint32_t maxBlock) {
    for (auto& e : nodes_) e.node->prepare(sampleRate, maxBlock);
    if (statsEnabled_) initStats();
  }

  void reset() {
    for (auto& e : nodes_) e.node->reset();
  }

  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) {
    if (nodes_.empty()) return;
    if (topoDirty_ || (topoOrder_.empty() && insertionOrder_.empty())) rebuildTopology();
    const size_t total = static_cast<size_t>(ctx.frames) * channels;
    if (outBuffers_.size() != nodes_.size()) outBuffers_.assign(nodes_.size(), std::vector<float>());
    if (work_.size() != total) work_.assign(total, 0.0f);

    // Process nodes by topo order; if empty, fall back to insertion order
    const std::vector<size_t>& order = topoOrder_.empty() ? insertionOrder_ : topoOrder_;
    // Prepare buffers
    for (size_t idx = 0; idx < nodes_.size(); ++idx) {
      auto& buf = outBuffers_[idx];
      if (buf.size() != total) buf.assign(total, 0.0f); else std::fill(buf.begin(), buf.end(), 0.0f);
    }

    for (size_t ni : order) {
      // Sum upstream edges into work_, honoring toPort (future multi-port semantics)
      std::fill(work_.begin(), work_.end(), 0.0f);
      auto upIt = upstream_.find(ni);
      // Optional: collect per-port sums
      std::unordered_map<uint32_t, std::vector<float>> portSums;
      if (upIt != upstream_.end()) {
        for (const auto& u : upIt->second) {
          const auto& src = outBuffers_[u.fromIndex];
          const float g = u.gain;
          auto& buf = portSums[u.toPort];
          if (buf.size() != total) buf.assign(total, 0.0f);
          // Generalized channel adaptation N<->M using declared port channel counts
          const uint32_t srcDeclared = (outPortChannels_.count(u.fromIndex) && outPortChannels_[u.fromIndex].count(u.fromPort))
                                        ? outPortChannels_[u.fromIndex][u.fromPort] : 0u;
          const uint32_t dstDeclared = (inPortChannels_.count(ni) && inPortChannels_[ni].count(u.toPort))
                                        ? inPortChannels_[ni][u.toPort] : 0u;
          adaptAndAccumulate(src.data(), buf, static_cast<uint32_t>(ctx.frames), channels, srcDeclared, dstDeclared, g);
        }
      }
      // Default mixing policy: only use port 0 as the node's main input.
      // Additional ports (e.g., sidechain) are not mixed into the main input and are provided
      // to port-aware nodes via portSums (e.g., CompressorNode reads port 1 separately).
      if (!portSums.empty()) {
        auto it0 = portSums.find(0u);
        if (it0 != portSums.end()) work_ = it0->second; else std::fill(work_.begin(), work_.end(), 0.0f);
      }
      Node* node = nodes_[ni].node.get();
      if (auto* d = dynamic_cast<DelayNode*>(node)) {
        // process effect in-place over summed input
        // copy work_ into node out buffer, then process in place
        auto& out = outBuffers_[ni];
        std::copy(work_.begin(), work_.end(), out.begin());
        d->processInPlace(ctx, out.data(), channels);
        if (statsEnabled_) accumulateStats(ni, out.data(), ctx.frames, channels);
      } else if (auto* c = dynamic_cast<CompressorNode*>(node)) {
        // If sidechain is provided on port 1, use it; else self-detect
        auto& out = outBuffers_[ni];
        std::copy(work_.begin(), work_.end(), out.begin());
        scWork_.assign(total, 0.0f);
        auto itSC = portSums.find(1u);
        if (itSC != portSums.end()) scWork_ = itSC->second;
        c->applySidechain(ctx, out.data(), scWork_.data(), channels);
        if (statsEnabled_) accumulateStats(ni, out.data(), ctx.frames, channels);
      } else if (auto* m = dynamic_cast<MeterNode*>(node)) {
        // pass-through input to output
        auto& out = outBuffers_[ni];
        std::copy(work_.begin(), work_.end(), out.begin());
        m->updateFromBuffer(out.data(), ctx.frames, channels);
        if (statsEnabled_) accumulateStats(ni, out.data(), ctx.frames, channels);
      } else {
        // generator/process node writes its own output (ignores inputs)
        node->process(ctx, outBuffers_[ni].data(), channels);
        if (statsEnabled_) accumulateStats(ni, outBuffers_[ni].data(), ctx.frames, channels);
      }
    }

    // Mix sinks to interleavedOut
    for (size_t i = 0; i < total; ++i) interleavedOut[i] = 0.0f;
    // Apply dry sends from connections
    if (!connections_.empty()) {
      std::unordered_map<std::string,size_t> idToIdx;
      idToIdx.reserve(nodes_.size());
      for (size_t i = 0; i < nodes_.size(); ++i) idToIdx.emplace(nodes_[i].id, i);
      for (const auto& e : connections_) {
        auto itF = idToIdx.find(e.from);
        if (itF == idToIdx.end()) continue;
        const float dry = e.dryPercent * (1.0f/100.0f);
        if (dry <= 0.0f) continue;
        const auto& src = outBuffers_[itF->second];
        for (size_t i = 0; i < total; ++i) interleavedOut[i] += src[i] * dry;
      }
    }
    for (size_t idx = 0; idx < nodes_.size(); ++idx) {
      const bool isSink = (downstream_.find(idx) == downstream_.end());
      float gain = 0.0f;
      if (mixer_) {
        for (const auto& ch : mixer_->channels()) if (ch.id == nodes_[idx].id) { gain = ch.gain; break; }
      }
      if (gain == 0.0f && isSink) gain = 1.0f;
      if (gain == 0.0f) continue;
      const auto& src = outBuffers_[idx];
      for (size_t i = 0; i < total; ++i) interleavedOut[i] += src[i] * gain;
    }
    if (mixer_) mixer_->process(ctx, interleavedOut, channels);
  }

private:
  struct NodeEntry { std::string id; std::unique_ptr<Node> node; };
  std::vector<NodeEntry> nodes_{};
  std::unique_ptr<MixerNode> mixer_{};
  std::vector<float> temp_{};
  std::vector<GraphSpec::Connection> connections_{};
  std::vector<std::vector<float>> outBuffers_{};
  std::vector<float> work_{};
  std::vector<float> scWork_{};

  struct UpEdge { size_t fromIndex; float gain; uint32_t fromPort; uint32_t toPort; };
  std::unordered_map<size_t, std::vector<UpEdge>> upstream_{};
  std::unordered_map<size_t, std::vector<size_t>> downstream_{};
  std::vector<size_t> topoOrder_{};
  std::vector<size_t> insertionOrder_{};
  bool topoDirty_ = false;
  // Port descriptors (channel counts); 0 means "match graph channels"
  std::unordered_map<size_t, std::unordered_map<uint32_t, uint32_t>> inPortChannels_{};
  std::unordered_map<size_t, std::unordered_map<uint32_t, uint32_t>> outPortChannels_{};
  std::unordered_map<std::string, size_t> idToIndex_{};

  // Per-node meters (accumulated across processing until reset)
  bool statsEnabled_ = false;
  struct NodeAccum { double peak = 0.0; long double sumSq = 0.0L; uint64_t count = 0; };
  std::vector<NodeAccum> nodeAccums_{};

  void initStats() {
    nodeAccums_.assign(nodes_.size(), NodeAccum{});
  }
  void accumulateStats(size_t nodeIdx, const float* interleaved, uint32_t frames, uint32_t channels) {
    if (nodeIdx >= nodeAccums_.size()) return;
    auto& a = nodeAccums_[nodeIdx];
    const size_t n = static_cast<size_t>(frames) * channels;
    for (size_t i = 0; i < n; ++i) {
      const double s = interleaved[i];
      const double aabs = std::fabs(s);
      if (aabs > a.peak) a.peak = aabs;
      a.sumSq += static_cast<long double>(s) * static_cast<long double>(s);
    }
    a.count += static_cast<uint64_t>(n);
  }

public:
  void enableStats(bool on) { statsEnabled_ = on; if (on) initStats(); }
  struct NodeMeter { std::string id; double peakDb; double rmsDb; };
  std::vector<NodeMeter> getNodeMeters(uint32_t /*channels*/) const {
    std::vector<NodeMeter> out;
    out.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) {
      const auto& a = nodeAccums_.empty() ? NodeAccum{} : nodeAccums_[i];
      double peakDb = (a.peak > 0.0) ? 20.0 * std::log10(a.peak) : -INFINITY;
      double rmsDb = -INFINITY;
      if (a.count > 0) {
        const double rms = std::sqrt(static_cast<double>(a.sumSq / static_cast<long double>(a.count)));
        rmsDb = (rms > 0.0) ? 20.0 * std::log10(rms) : -INFINITY;
      }
      out.push_back(NodeMeter{nodes_[i].id, peakDb, rmsDb});
    }
    return out;
  }

  void rebuildTopology() {
    insertionOrder_.clear(); insertionOrder_.reserve(nodes_.size());
    for (size_t i = 0; i < nodes_.size(); ++i) insertionOrder_.push_back(i);
    // Map id->index
    std::unordered_map<std::string,size_t> idToIdx;
    for (size_t i = 0; i < nodes_.size(); ++i) idToIdx[nodes_[i].id] = i;
    upstream_.clear(); downstream_.clear();
    std::vector<int> indeg(nodes_.size(), 0);
    for (const auto& e : connections_) {
      auto itF = idToIdx.find(e.from), itT = idToIdx.find(e.to);
      if (itF == idToIdx.end() || itT == idToIdx.end()) continue;
      const float g = e.gainPercent * (1.0f/100.0f);
      upstream_[itT->second].push_back(UpEdge{itF->second, g, e.fromPort, e.toPort});
      downstream_[itF->second].push_back(itT->second);
      indeg[itT->second]++;
    }
    // Kahn topo
    topoOrder_.clear(); topoOrder_.reserve(nodes_.size());
    std::vector<size_t> q; q.reserve(nodes_.size());
    for (size_t i=0;i<nodes_.size();++i) if (indeg[i]==0) q.push_back(i);
    for (size_t qi=0; qi<q.size(); ++qi) {
      auto u = q[qi]; topoOrder_.push_back(u);
      auto it = downstream_.find(u);
      if (it != downstream_.end()) {
        for (auto v : it->second) { if (--indeg[v]==0) q.push_back(v); }
      }
    }
    if (topoOrder_.size() != nodes_.size()) {
      // cycle or disconnected nodes; keep insertion order as fallback
      topoOrder_.clear();
    }
    topoDirty_ = false;
  }

private:
  // Adapt src declared channels to dst declared channels within a graph that runs at 'graphCh' channels
  // and accumulate into dst buffer with gain. Declared channel count 0 means "graph default".
  void adaptAndAccumulate(const float* src, std::vector<float>& dst,
                          uint32_t frames, uint32_t graphCh,
                          uint32_t srcDeclared, uint32_t dstDeclared,
                          float gain) {
    const size_t total = static_cast<size_t>(frames) * static_cast<size_t>(graphCh);
    const uint32_t srcCh = (srcDeclared == 0u) ? graphCh : srcDeclared;
    const uint32_t dstCh = (dstDeclared == 0u) ? graphCh : dstDeclared;

    // If either side is mono, average to mono and duplicate across graph channels
    if (srcCh == 1u || dstCh == 1u) {
      for (uint32_t f = 0; f < frames; ++f) {
        // Average across graph channels (robust even if actual src duplicated)
        double sum = 0.0;
        for (uint32_t c = 0; c < graphCh; ++c) sum += static_cast<double>(src[static_cast<size_t>(f)*graphCh + c]);
        const float m = static_cast<float>(sum / static_cast<double>(graphCh));
        for (uint32_t c = 0; c < graphCh; ++c) dst[static_cast<size_t>(f)*graphCh + c] += m * gain;
      }
      return;
    }

    // Otherwise, pass-through channel-wise (graph channels act as max width).
    // If declared counts differ but both >1, use modulo mapping as a simple adapter.
    if (srcCh == dstCh) {
      for (size_t i = 0; i < total; ++i) dst[i] += src[i] * gain;
      return;
    }
    for (uint32_t f = 0; f < frames; ++f) {
      for (uint32_t c = 0; c < graphCh; ++c) {
        const uint32_t s = (c % graphCh); // graph-sized interleaved
        dst[static_cast<size_t>(f)*graphCh + c] += src[static_cast<size_t>(f)*graphCh + s] * gain;
      }
    }
  }
};


