#pragma once

#include <memory>
#include <string>
#include "GraphConfig.hpp"
#include "../instruments/kick/KickFactory.hpp"
#include "../instruments/clap/ClapFactory.hpp"
#include "TransportNode.hpp"
// Mixer is not created via NodeFactory; it is set on Graph from GraphSpec.mixer

inline std::unique_ptr<Node> createNodeFromSpec(const NodeSpec& spec) {
  if (spec.type == "kick") {
    return makeKickNodeFromParamsJson(spec.paramsJson);
  }
  if (spec.type == "clap") {
    return makeClapNodeFromParamsJson(spec.paramsJson);
  }
  if (spec.type == "transport") {
    auto t = std::make_unique<TransportNode>();
    // Minimal: parse pattern from paramsJson
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      t->bpm = static_cast<float>(j.value("bpm", 120.0));
      t->lengthBars = j.value("lengthBars", 1u);
      t->resolution = j.value("resolution", 16u);
      t->swingPercent = static_cast<float>(j.value("swingPercent", 0.0));
      if (j.contains("pattern")) {
        t->pattern.nodeId = j["pattern"].value("nodeId", "");
        t->pattern.steps = j["pattern"].value("steps", "");
      }
    } catch (...) {}
    return t;
  }
  // Unknown node type
  std::fprintf(stderr, "Warning: Unknown node type '%s' (id='%s')\n", spec.type.c_str(), spec.id.c_str());
  return nullptr;
}


