#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include "ClapNode.hpp"
#include "../../core/ParamMap.hpp"

inline std::unique_ptr<ClapNode> makeClapNodeFromParamsJson(const std::string& paramsJson) {
  nlohmann::json j = nlohmann::json::parse(paramsJson);
  ClapParams p;
  if (j.contains("ampDecayMs")) p.ampDecayMs = j.at("ampDecayMs").get<float>();
  if (j.contains("gain")) p.gain = j.at("gain").get<float>();
  if (j.contains("bpm")) p.bpm = j.at("bpm").get<float>();
  if (j.contains("loop")) p.loop = j.at("loop").get<bool>();
  // Clamp using ParamMap ranges
  p.ampDecayMs = clampToRange(kClapParamMap, "AMP_DECAY_MS", p.ampDecayMs);
  p.gain = clampToRange(kClapParamMap, "GAIN", p.gain);
  return std::make_unique<ClapNode>(p);
}



