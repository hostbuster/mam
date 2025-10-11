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
  struct CommandSpec {
    uint64_t sampleTime = 0;
    std::string nodeId;
    std::string type; // "Trigger" | "SetParam" | "SetParamRamp"
    std::string paramName; // optional: named param (e.g., "F0")
    uint16_t paramId = 0;
    float value = 0.0f;
    float rampMs = 0.0f;
  };
  std::vector<CommandSpec> commands;

  struct TransportLock { uint32_t step = 0; std::string paramName; uint16_t paramId = 0; float value = 0.0f; float rampMs = 0.0f; };
  struct TransportPattern { std::string nodeId; std::string steps; std::vector<TransportLock> locks; };
  struct Transport {
    float bpm = 120.0f;
    uint32_t lengthBars = 1;   // number of bars
    uint32_t resolution = 16;  // steps per bar
    float swingPercent = 0.0f; // 0..100, applied to odd steps
    struct TempoPoint { uint32_t bar = 0; float bpm = 120.0f; };
    std::vector<TempoPoint> tempoRamps; // stepwise bpm at given bar indices
    std::vector<TransportPattern> patterns;
  };
  bool hasTransport = false;
  Transport transport;
};

// Parse file into GraphSpec using nlohmann/json
GraphSpec loadGraphSpecFromJsonFile(const std::string& path);


