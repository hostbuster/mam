#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "../core/GraphConfig.hpp"

inline std::vector<GraphSpec::CommandSpec> generateCommandsFromTransport(const GraphSpec::Transport& tr, uint32_t sampleRate) {
  std::vector<GraphSpec::CommandSpec> out;
  const uint32_t stepsPerBar = tr.resolution ? tr.resolution : 16u;
  const uint64_t totalBars = tr.lengthBars ? tr.lengthBars : 1;
  const uint64_t totalSteps = totalBars * stepsPerBar;
  auto bpmAtBar = [&](uint32_t barIndex) -> double {
    double bpm = tr.bpm;
    for (const auto& p : tr.tempoRamps) {
      if (p.bar <= barIndex) bpm = p.bpm;
    }
    return bpm;
  };
  auto framesPerStepAtBar = [&](uint32_t barIndex) -> uint64_t {
    const double secPerBeat = 60.0 / bpmAtBar(barIndex);
    const double secPerBar = 4.0 * secPerBeat;
    const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sampleRate + 0.5);
    return stepsPerBar ? (framesPerBar / stepsPerBar) : framesPerBar;
  };

  // For parity with realtime, compute absolute sample position by cumulatively
  // summing frames per step and adding swing on odd steps within each bar.
  for (uint64_t step = 0; step < totalSteps; ++step) {
    const uint32_t barIndex = static_cast<uint32_t>(step / stepsPerBar);
    const uint32_t withinBar = static_cast<uint32_t>(step % stepsPerBar);
    const uint64_t baseFramesPerStep = framesPerStepAtBar(barIndex);

    // Accumulate all previous bars
    uint64_t stepStart = 0;
    for (uint32_t b = 0; b < barIndex; ++b) stepStart += framesPerStepAtBar(b) * stepsPerBar;
    // Within current bar, accumulate previous steps with symmetric swing (+ on even, - on odd)
    for (uint32_t s = 0; s < withinBar; ++s) {
      const bool prevEven = (s % 2) == 0;
      const double swingLin = (tr.swingPercent / 100.0) * 0.5; // +/- 50% of step
      const double shaped = (tr.swingExponent != 1.0f)
        ? std::pow(std::abs(swingLin), static_cast<double>(tr.swingExponent)) * (swingLin >= 0.0 ? 1.0 : -1.0)
        : swingLin;
      const double swingAmt = static_cast<double>(baseFramesPerStep) * shaped;
      const double delta = static_cast<double>(baseFramesPerStep) + (prevEven ? swingAmt : -swingAmt);
      stepStart += static_cast<uint64_t>(delta + 0.5);
    }
    // Now at this step's start time; emit triggers and locks per pattern
    for (const auto& pat : tr.patterns) {
      if (pat.steps.empty()) continue;
      const size_t idx = static_cast<size_t>(withinBar % static_cast<uint32_t>(pat.steps.size()));
      if (idx < pat.steps.size() && pat.steps[idx] == 'x') {
        GraphSpec::CommandSpec c;
        c.sampleTime = stepStart;
        c.nodeId = pat.nodeId;
        c.type = "Trigger";
        out.push_back(c);
      }
      // Locks for this step
      for (const auto& L : pat.locks) {
        if (L.step != withinBar) continue;
        GraphSpec::CommandSpec lc;
        lc.sampleTime = stepStart;
        lc.nodeId = pat.nodeId;
        lc.type = (L.rampMs > 0.0f) ? std::string("SetParamRamp") : std::string("SetParam");
        lc.paramName = L.paramName;
        lc.paramId = L.paramId;
        lc.value = L.value;
        lc.rampMs = L.rampMs;
        out.push_back(lc);
      }
    }
  }

  // Stable sort ensures deterministic ordering on same-sample events (locks before triggers or vice versa)
  std::stable_sort(out.begin(), out.end(), [](const GraphSpec::CommandSpec& a, const GraphSpec::CommandSpec& b){
    if (a.sampleTime != b.sampleTime) return a.sampleTime < b.sampleTime;
    if (a.nodeId != b.nodeId) return a.nodeId < b.nodeId;
    if (a.type != b.type) return a.type < b.type;
    if (a.paramId != b.paramId) return a.paramId < b.paramId;
    return a.value < b.value;
  });

  return out;
}


