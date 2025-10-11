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
  struct PatternLock { uint32_t step = 0; uint16_t paramId = 0; float value = 0.0f; float rampMs = 0.0f; };
  struct Pattern { std::string nodeId; std::string steps; std::vector<PatternLock> locks; };
  struct TempoPoint { uint32_t bar = 0; float bpm = 120.0f; };

  TransportNode() = default;

  const char* name() const override { return "TransportNode"; }

  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    sampleRate_ = sampleRate;
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
  std::vector<TempoPoint> tempoRamps; // stepwise bpm at given bar indices
  std::vector<Pattern> patterns;      // multiple patterns supported

private:
  double sampleRate_ = 48000.0;
  uint64_t nextStepStartAbs_ = 0;
  uint32_t stepIndex_ = 0;

public:
  template <typename EmitFn>
  void emitIfMatch(SampleTime absStart, EmitFn emit) {
    if (absStart != nextStepStartAbs_) return;
    const uint32_t stepsPerBar = resolution ? resolution : 16u;
    const uint64_t totalSteps = (lengthBars ? lengthBars : 1u) * stepsPerBar;
    const uint32_t barIndex = stepsPerBar ? (stepIndex_ / stepsPerBar) : 0u;
    const uint32_t withinBar = stepsPerBar ? (stepIndex_ % stepsPerBar) : 0u;

    // Emit triggers for all patterns with 'x' at this step
    for (const auto& pat : patterns) {
      if (pat.steps.empty()) continue;
      const size_t idx = static_cast<size_t>(withinBar % static_cast<uint32_t>(pat.steps.size()));
      if (idx < pat.steps.size() && pat.steps[idx] == 'x' && !pat.nodeId.empty()) {
        Command c{}; c.sampleTime = absStart; c.nodeId = pat.nodeId.c_str(); c.type = CommandType::Trigger;
        emit(c);
      }
      // Emit parameter locks scheduled for this step (paramId-based)
      for (const auto& L : pat.locks) {
        if (L.step != withinBar || L.paramId == 0) continue;
        Command lc{}; lc.sampleTime = absStart; lc.nodeId = pat.nodeId.c_str();
        lc.type = (L.rampMs > 0.0f) ? CommandType::SetParamRamp : CommandType::SetParam;
        lc.paramId = L.paramId; lc.value = L.value; lc.rampMs = L.rampMs;
        emit(lc);
      }
    }

    // Advance to next step start, considering tempo ramps and swing on odd steps
    const double bpmNow = bpmAtBar(barIndex);
    const double secPerBeat = bpmNow > 0.0 ? (60.0 / bpmNow) : 0.5;
    const double secPerBar = 4.0 * secPerBeat;
    const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sampleRate_ + 0.5);
    const uint64_t baseFramesPerStep = stepsPerBar ? (framesPerBar / stepsPerBar) : framesPerBar;
    const bool isOdd = (withinBar % 2u) == 1u;
    const double swingFrames = isOdd ? (static_cast<double>(baseFramesPerStep) * (swingPercent / 100.0) * 0.5) : 0.0;
    nextStepStartAbs_ += baseFramesPerStep + static_cast<uint64_t>(swingFrames + 0.5);

    // Increment and wrap step index across total length
    stepIndex_ = (totalSteps > 0) ? ((stepIndex_ + 1) % static_cast<uint32_t>(totalSteps)) : (stepIndex_ + 1);
  }

  SampleTime nextEventSample() const { return nextStepStartAbs_; }

private:
  double bpmAtBar(uint32_t barIndex) const {
    double cur = bpm;
    for (const auto& p : tempoRamps) {
      if (p.bar <= barIndex) cur = p.bpm;
    }
    return cur;
  }
};


