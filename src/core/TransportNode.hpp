#pragma once

#include "Node.hpp"
#include <string>
#include <vector>
#include <functional>

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
    const double secPerBeat = bpm > 0.0f ? (60.0 / static_cast<double>(bpm)) : 0.5; // default
    const double secPerBar = 4.0 * secPerBeat;
    const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sampleRate_ + 0.5);
    const uint32_t stepsPerBar = resolution ? resolution : 16u;
    framesPerStep_ = stepsPerBar ? (framesPerBar / stepsPerBar) : framesPerBar;
    nextStepStartAbs_ = 0;
    stepIndex_ = 0;
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
  uint64_t framesPerStep_ = 0;
  uint64_t nextStepStartAbs_ = 0;
  uint32_t stepIndex_ = 0;

public:
  template <typename EmitFn>
  void emitIfMatch(SampleTime absStart, EmitFn emit) {
    if (pattern.steps.empty()) return;
    if (absStart != nextStepStartAbs_) return;
    const size_t idx = static_cast<size_t>(stepIndex_ % pattern.steps.size());
    if (pattern.steps[idx] == 'x') {
      Command c{}; c.sampleTime = absStart; c.nodeId = pattern.nodeId.c_str(); c.type = CommandType::Trigger;
      emit(c);
    }
    // advance to next step start with optional swing on odd steps
    const bool isOdd = (stepIndex_ % 2u) == 1u;
    const double swing = isOdd ? (static_cast<double>(framesPerStep_) * (swingPercent / 100.0) * 0.5) : 0.0;
    nextStepStartAbs_ += framesPerStep_ + static_cast<uint64_t>(swing + 0.5);
    stepIndex_++;
  }

  SampleTime nextEventSample() const { return nextStepStartAbs_; }
};


