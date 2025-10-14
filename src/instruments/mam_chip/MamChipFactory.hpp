#pragma once

#include "MamChipNode.hpp"
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

inline std::unique_ptr<MamChipNode> makeMamChipFromParamsJson(const std::string& paramsJson) {
  auto node = std::make_unique<MamChipNode>();
  try {
    if (!paramsJson.empty()) {
      nlohmann::json j = nlohmann::json::parse(paramsJson);
      // wave: accept string enum ("square"|"tri"|"saw") or numeric
      if (j.contains("wave"))       {
        Command c{}; c.type=CommandType::SetParam; c.paramId=1;  c.value = 0.0f;
        if (j["wave"].is_string()) {
          const std::string w = j.value("wave", std::string("square"));
          if (w == "square" || w == "pulse") c.value = 0.0f;
          else if (w == "tri" || w == "triangle") c.value = 1.0f;
          else if (w == "saw" || w == "sawtooth") c.value = 2.0f;
        } else {
          c.value = static_cast<float>(j.value("wave", 0));
        }
        node->handleEvent(c);
      }
      if (j.contains("note"))       { Command c{}; c.type=CommandType::SetParam; c.paramId=2;  c.value = static_cast<float>(j.value("note", 60));      node->handleEvent(c); }
      if (j.contains("gain"))       { Command c{}; c.type=CommandType::SetParam; c.paramId=5;  c.value = static_cast<float>(j.value("gain", 0.9));     node->handleEvent(c); }
      if (j.contains("pan"))        { Command c{}; c.type=CommandType::SetParam; c.paramId=6;  c.value = static_cast<float>(j.value("pan", 0.0));      node->handleEvent(c); }
      if (j.contains("attackMs"))   { Command c{}; c.type=CommandType::SetParam; c.paramId=7;  c.value = static_cast<float>(j.value("attackMs", 10));  node->handleEvent(c); }
      if (j.contains("decayMs"))    { Command c{}; c.type=CommandType::SetParam; c.paramId=8;  c.value = static_cast<float>(j.value("decayMs", 120));  node->handleEvent(c); }
      if (j.contains("sustain"))    { Command c{}; c.type=CommandType::SetParam; c.paramId=9;  c.value = static_cast<float>(j.value("sustain", 0.7));  node->handleEvent(c); }
      if (j.contains("releaseMs"))  { Command c{}; c.type=CommandType::SetParam; c.paramId=10; c.value = static_cast<float>(j.value("releaseMs", 200)); node->handleEvent(c); }
      if (j.contains("pulseWidth")) { Command c{}; c.type=CommandType::SetParam; c.paramId=4;  c.value = static_cast<float>(j.value("pulseWidth", 0.5)); node->handleEvent(c); }
      if (j.contains("noiseMix"))   { Command c{}; c.type=CommandType::SetParam; c.paramId=11; c.value = static_cast<float>(j.value("noiseMix", 0.0)); node->handleEvent(c); }
    }
  } catch (...) {}
  return node;
}


