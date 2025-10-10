#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct NodeSpec {
  std::string id;
  std::string type;
  // raw JSON for params will be parsed per type in the factory
  std::string paramsJson;
};

struct GraphSpec {
  int version = 1;
  uint32_t sampleRate = 48000;
  uint32_t channels = 2;
  std::vector<NodeSpec> nodes;
  struct MixerInput { std::string id; float gainPercent = 100.0f; };
  struct Mixer { std::vector<MixerInput> inputs; float masterPercent = 100.0f; bool softClip = true; };
  bool hasMixer = false;
  Mixer mixer;
};

// Parse file into GraphSpec using nlohmann/json
GraphSpec loadGraphSpecFromJsonFile(const std::string& path);


