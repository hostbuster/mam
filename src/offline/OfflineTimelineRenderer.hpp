#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>
#include <string>
#include "../core/Graph.hpp"
#include "../core/GraphConfig.hpp"
#include "../core/Command.hpp"

inline std::vector<float> renderGraphWithCommands(Graph& graph,
                                                  const std::vector<GraphSpec::CommandSpec>& cmds,
                                                  uint32_t sampleRate,
                                                  uint32_t channels,
                                                  uint64_t frames) {
  graph.prepare(sampleRate, 1024);
  graph.reset();

  std::vector<float> out;
  out.resize(static_cast<size_t>(frames * channels), 0.0f);

  // Copy and sort commands by time
  std::vector<GraphSpec::CommandSpec> commands = cmds;
  std::sort(commands.begin(), commands.end(), [](const auto& a, const auto& b){ return a.sampleTime < b.sampleTime; });

  const uint32_t block = 1024;
  size_t cmdIndex = 0;
  for (uint64_t f = 0; f < frames; f += block) {
    const uint32_t thisBlock = static_cast<uint32_t>(std::min<uint64_t>(block, frames - f));
    const uint64_t blockStart = f;
    const uint64_t cutoff = f + thisBlock;

    // Split offsets within block based on command times
    std::vector<uint32_t> splits;
    splits.reserve(8);
    splits.push_back(0);
    size_t idx = cmdIndex;
    while (idx < commands.size() && commands[idx].sampleTime < cutoff) {
      if (commands[idx].sampleTime >= blockStart) {
        splits.push_back(static_cast<uint32_t>(commands[idx].sampleTime - blockStart));
      }
      ++idx;
    }
    splits.push_back(thisBlock);
    std::sort(splits.begin(), splits.end());
    splits.erase(std::unique(splits.begin(), splits.end()), splits.end());

    // Process segments
    for (size_t si = 0; si + 1 < splits.size(); ++si) {
      const uint32_t segStart = splits[si];
      const uint32_t segEnd = splits[si + 1];
      const uint32_t segFrames = segEnd - segStart;
      if (segFrames == 0) continue;

      const uint64_t segAbs = blockStart + segStart;

      // Deliver events exactly at segAbs
      size_t di = cmdIndex;
      while (di < commands.size() && commands[di].sampleTime < segAbs) ++di;
      for (; di < commands.size() && commands[di].sampleTime == segAbs; ++di) {
        Command c{};
        c.sampleTime = segAbs;
        c.nodeId = commands[di].nodeId.c_str();
        if (commands[di].type == std::string("Trigger")) c.type = CommandType::Trigger;
        else if (commands[di].type == std::string("SetParam")) c.type = CommandType::SetParam;
        else if (commands[di].type == std::string("SetParamRamp")) c.type = CommandType::SetParamRamp;
        c.paramId = commands[di].paramId;
        c.value = commands[di].value;
        c.rampMs = commands[di].rampMs;
        graph.forEachNode([&](const std::string& id, Node& n){ if (c.nodeId && id == c.nodeId) n.handleEvent(c); });
      }

      ProcessContext ctx{}; ctx.sampleRate = sampleRate; ctx.frames = segFrames; ctx.blockStart = segAbs;
      float* outPtr = out.data() + static_cast<size_t>((blockStart + segStart) * channels);
      graph.process(ctx, outPtr, channels);
    }

    // Advance cmdIndex beyond this block
    while (cmdIndex < commands.size() && commands[cmdIndex].sampleTime < cutoff) ++cmdIndex;
  }

  return out;
}


