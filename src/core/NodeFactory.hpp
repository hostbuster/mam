#pragma once

#include <memory>
#include <string>
#include "GraphConfig.hpp"
#include "../instruments/kick/KickFactory.hpp"
#include "../instruments/clap/ClapFactory.hpp"
// Mixer is not created via NodeFactory; it is set on Graph from GraphSpec.mixer

inline std::unique_ptr<Node> createNodeFromSpec(const NodeSpec& spec) {
  if (spec.type == "kick") {
    return makeKickNodeFromParamsJson(spec.paramsJson);
  }
  if (spec.type == "clap") {
    return makeClapNodeFromParamsJson(spec.paramsJson);
  }
  // Unknown node type
  std::fprintf(stderr, "Warning: Unknown node type '%s' (id='%s')\n", spec.type.c_str(), spec.id.c_str());
  return nullptr;
}


