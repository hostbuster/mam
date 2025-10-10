#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include "ClapNode.hpp"

inline std::unique_ptr<ClapNode> makeClapNodeFromParamsJson(const std::string& paramsJson) {
  nlohmann::json j = nlohmann::json::parse(paramsJson);
  ClapParams p;
  if (j.contains("ampDecayMs")) p.ampDecayMs = j.at("ampDecayMs").get<float>();
  if (j.contains("gain")) p.gain = j.at("gain").get<float>();
  if (j.contains("bpm")) p.bpm = j.at("bpm").get<float>();
  if (j.contains("loop")) p.loop = j.at("loop").get<bool>();
  return std::make_unique<ClapNode>(p);
}



