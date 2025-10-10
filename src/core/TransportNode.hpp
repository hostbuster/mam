#pragma once

#include "Node.hpp"
#include <string>
#include <vector>

// Draft TransportNode: non-audio node intended to drive triggers/param-locks.
// Current realtime path still uses pre-enqueued horizon; offline uses generator.
// This node is a scaffold for future integration with realtime event routing.
class TransportNode final : public Node {
public:
  struct Pattern { std::string nodeId; std::string steps; };

  TransportNode() = default;

  const char* name() const override { return "TransportNode"; }

  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    sampleRate_ = sampleRate;
  }

  void reset() override {}

  void process(ProcessContext /*ctx*/, float* interleavedOut, uint32_t channels) override {
    // No audio output; write silence if connected
    (void)interleavedOut; (void)channels;
  }

  void handleEvent(const Command& /*cmd*/) override {}

  // State (schema-aligned)
  float bpm = 120.0f;
  uint32_t lengthBars = 1;
  uint32_t resolution = 16;
  float swingPercent = 0.0f;
  Pattern pattern; // single-pattern scaffold; future: vector<Pattern>

private:
  double sampleRate_ = 48000.0;
};


