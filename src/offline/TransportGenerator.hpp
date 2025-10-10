#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "../core/GraphConfig.hpp"

inline std::vector<GraphSpec::CommandSpec> generateCommandsFromTransport(const GraphSpec::Transport& tr, uint32_t sampleRate) {
  std::vector<GraphSpec::CommandSpec> out;
  const double secPerBeat = 60.0 / static_cast<double>(tr.bpm);
  const double secPerBar = 4.0 * secPerBeat; // 4/4
  const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sampleRate + 0.5);
  const uint32_t stepsPerBar = tr.resolution;
  const uint64_t framesPerStep = stepsPerBar ? (framesPerBar / stepsPerBar) : framesPerBar;
  const uint64_t totalBars = tr.lengthBars ? tr.lengthBars : 1;
  const uint64_t totalSteps = totalBars * stepsPerBar;

  for (const auto& pat : tr.patterns) {
    for (uint64_t step = 0; step < totalSteps; ++step) {
      const size_t idx = static_cast<size_t>(step % pat.steps.size());
      if (idx < pat.steps.size() && pat.steps[idx] == 'x') {
        GraphSpec::CommandSpec c;
        c.sampleTime = step * framesPerStep;
        c.nodeId = pat.nodeId;
        c.type = "Trigger";
        out.push_back(c);
      }
    }
  }

  return out;
}


