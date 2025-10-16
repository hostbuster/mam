#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct NodeSpec {
  struct ModLfoSpec {
    uint16_t id = 0;
    std::string wave; // "sine"|"triangle"|"saw"|"square"
    float freqHz = 0.5f;
    float phase01 = 0.0f;
  };
  struct ModRouteSpec {
    uint16_t sourceId = 0;
    uint16_t destParamId = 0;
    std::string destParamName; // optional, resolves to id per node type
    float depth = 0.0f;
    float offset = 0.0f;
    std::string map; // "linear" | "exp" (optional)
    float minValue = 0.0f; // optional mapping range; if min<max, takes precedence over depth/offset
    float maxValue = 0.0f;
  };
  struct ModSpec {
    std::vector<ModLfoSpec> lfos;
    std::vector<ModRouteSpec> routes;
    bool has = false;
  };
  struct PortDesc {
    uint32_t index = 0;
    std::string name;
    std::string type;    // "audio" | "control" | "event" | "midi"
    uint32_t channels = 0; // optional
    std::string role;    // "main" | "sidechain" | "aux"
  };
  struct PortsSpec {
    std::vector<PortDesc> inputs;
    std::vector<PortDesc> outputs;
    bool has = false;
  };

  std::string id;
  std::string type;
  // raw JSON for params will be parsed per type in the factory
  std::string paramsJson;
  PortsSpec ports;
  ModSpec mod;
};

struct GraphSpec {
  std::string description; // optional human-readable description
  int version = 1;
  uint32_t sampleRate = 48000;
  uint32_t channels = 2;
  uint32_t randomSeed = 0; // 0 means unspecified
  std::vector<NodeSpec> nodes;
  struct Connection {
    std::string from;
    std::string to;
    float gainPercent = 100.0f; // wet level into downstream
    float dryPercent = 0.0f;    // optional dry send to output mix
    uint32_t fromPort = 0;      // future: multi-port routing (audio/control)
    uint32_t toPort = 0;
  };
  std::vector<Connection> connections; // MVP: order only
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
    float swingExponent = 1.0f; // shape swing amount: 1=linear, >1 softer at low %, <1 stronger
    struct TempoPoint { uint32_t bar = 0; float bpm = 120.0f; };
    std::vector<TempoPoint> tempoRamps; // stepwise bpm at given bar indices
    std::vector<TransportPattern> patterns;
  };
  bool hasTransport = false;
  Transport transport;
};

// Parse file into GraphSpec using nlohmann/json
GraphSpec loadGraphSpecFromJsonFile(const std::string& path);


