#pragma once

#include <memory>
#include <string>
#include "GraphConfig.hpp"
#include "../instruments/kick/KickFactory.hpp"
#include "../instruments/clap/ClapFactory.hpp"
#include "TransportNode.hpp"
#include "DelayNode.hpp"
#include "MeterNode.hpp"
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
    // Parse transport params: bpm, resolution, swing, ramps, patterns
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      t->bpm = static_cast<float>(j.value("bpm", 120.0));
      t->lengthBars = j.value("lengthBars", 1u);
      t->resolution = j.value("resolution", 16u);
      t->swingPercent = static_cast<float>(j.value("swingPercent", 0.0));
      if (j.contains("tempoRamps")) {
        for (const auto& tp : j.at("tempoRamps")) {
          TransportNode::TempoPoint p{};
          p.bar = tp.value("bar", 0u);
          p.bpm = static_cast<float>(tp.value("bpm", t->bpm));
          t->tempoRamps.push_back(p);
        }
      }
      if (j.contains("patterns")) {
        for (const auto& pj : j.at("patterns")) {
          TransportNode::Pattern p{};
          p.nodeId = pj.value("nodeId", "");
          p.steps = pj.value("steps", "");
          // optional inline locks using paramId for realtime path
          if (pj.contains("locks")) {
            for (const auto& lk : pj.at("locks")) {
              TransportNode::PatternLock L{};
              L.step = lk.value("step", 0u);
              L.paramId = static_cast<uint16_t>(lk.value("paramId", 0));
              L.value = lk.value("value", 0.0f);
              L.rampMs = lk.value("rampMs", 0.0f);
              p.locks.push_back(L);
            }
          }
          t->patterns.push_back(p);
        }
      } else if (j.contains("pattern")) { // backwards-compat
        TransportNode::Pattern p{};
        p.nodeId = j["pattern"].value("nodeId", "");
        p.steps = j["pattern"].value("steps", "");
        t->patterns.push_back(p);
      }
    } catch (...) {}
    return t;
  }
  if (spec.type == "delay") {
    auto d = std::make_unique<DelayNode>();
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      if (j.contains("delayMs")) d->setDelayMs(static_cast<float>(j.value("delayMs", 350.0)));
      if (j.contains("feedback")) d->feedback = static_cast<float>(j.value("feedback", 0.35));
      if (j.contains("mix")) d->mix = static_cast<float>(j.value("mix", 0.25));
    } catch (...) {}
    return d;
  }
  if (spec.type == "meter") {
    // Meter is an effect-style node; creation from params only stores target for documentation.
    return std::make_unique<MeterNode>(spec.id);
  }
  // Unknown node type
  std::fprintf(stderr, "Warning: Unknown node type '%s' (id='%s')\n", spec.type.c_str(), spec.id.c_str());
  return nullptr;
}


