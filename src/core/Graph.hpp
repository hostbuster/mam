#pragma once

#include <vector>
#include <memory>
#include <functional>
#include <string>
#include "Node.hpp"
#include "MixerNode.hpp"

class Graph {
public:
  void addNode(std::string id, std::unique_ptr<Node> node) {
    nodes_.push_back(NodeEntry{std::move(id), std::move(node)});
  }
  void setMixer(std::unique_ptr<MixerNode> mixer) { mixer_ = std::move(mixer); }

  // Iterate nodes with their ids (read-only access to Node&). Not realtime-safe to mutate graph.
  void forEachNode(const std::function<void(const std::string&, Node&)>& fn) {
    for (auto& e : nodes_) fn(e.id, *e.node);
  }

  void prepare(double sampleRate, uint32_t maxBlock) {
    for (auto& e : nodes_) e.node->prepare(sampleRate, maxBlock);
  }

  void reset() {
    for (auto& e : nodes_) e.node->reset();
  }

  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) {
    if (nodes_.empty()) return;
    const size_t total = static_cast<size_t>(ctx.frames) * channels;
    // Zero output
    for (size_t i = 0; i < total; ++i) interleavedOut[i] = 0.0f;
    // Mix all nodes
    if (temp_.size() != total) temp_.resize(total);
    for (auto& e : nodes_) {
      // Clear temp
      std::fill(temp_.begin(), temp_.end(), 0.0f);
      e.node->process(ctx, temp_.data(), channels);
      float gain = 1.0f;
      if (mixer_) {
        for (const auto& ch : mixer_->channels()) {
          if (ch.id == e.id) { gain = ch.gain; break; }
        }
      }
      if (gain != 1.0f) {
        for (size_t i = 0; i < total; ++i) interleavedOut[i] += temp_[i] * gain;
      } else {
        for (size_t i = 0; i < total; ++i) interleavedOut[i] += temp_[i];
      }
    }
    if (mixer_) mixer_->process(ctx, interleavedOut, channels);
  }

private:
  struct NodeEntry { std::string id; std::unique_ptr<Node> node; };
  std::vector<NodeEntry> nodes_{};
  std::unique_ptr<MixerNode> mixer_{};
  std::vector<float> temp_{};
};


