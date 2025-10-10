#pragma once

#include <memory>
#include <nlohmann/json.hpp>
#include "KickNode.hpp"

inline std::unique_ptr<KickNode> makeKickNodeFromParamsJson(const std::string& paramsJson) {
  nlohmann::json j = nlohmann::json::parse(paramsJson);
  KickParams p;
  if (j.contains("f0")) p.startFreqHz = j.at("f0").get<float>();
  if (j.contains("fend")) p.endFreqHz = j.at("fend").get<float>();
  if (j.contains("pitchDecayMs")) p.pitchDecayMs = j.at("pitchDecayMs").get<float>();
  if (j.contains("ampDecayMs")) p.ampDecayMs = j.at("ampDecayMs").get<float>();
  if (j.contains("gain")) p.gain = j.at("gain").get<float>();
  if (j.contains("click")) p.click = j.at("click").get<float>();
  if (j.contains("bpm")) p.bpm = j.at("bpm").get<float>();
  if (j.contains("loop")) p.loop = j.at("loop").get<bool>();
  return std::make_unique<KickNode>(p);
}



