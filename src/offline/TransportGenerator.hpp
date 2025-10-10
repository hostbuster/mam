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

  for (const auto& pat : tr.patterns) {
    for (uint64_t step = 0; step < totalSteps; ++step) {
      const size_t idx = pat.steps.empty() ? 0u : static_cast<size_t>(step % pat.steps.size());
      if (!pat.steps.empty() && idx < pat.steps.size() && pat.steps[idx] == 'x') {
        const uint32_t barIndex = static_cast<uint32_t>(step / stepsPerBar);
        const uint64_t baseFramesPerStep = framesPerStepAtBar(barIndex);
        uint64_t stepStart = 0;
        // accumulate frames up to this step considering ramps
        for (uint32_t b = 0; b < barIndex; ++b) stepStart += framesPerStepAtBar(b) * stepsPerBar;
        const uint32_t withinBar = static_cast<uint32_t>(step % stepsPerBar);
        // swing: delay odd steps by swingPercent of half-step
        const bool isOdd = (withinBar % 2) == 1;
        const double swingFrames = isOdd ? (static_cast<double>(baseFramesPerStep) * (tr.swingPercent / 100.0) * 0.5) : 0.0;
        stepStart += static_cast<uint64_t>(withinBar) * baseFramesPerStep + static_cast<uint64_t>(swingFrames + 0.5);

        GraphSpec::CommandSpec c;
        c.sampleTime = stepStart;
        c.nodeId = pat.nodeId;
        c.type = "Trigger";
        out.push_back(c);
      }
    }
  }

  return out;
}


