#pragma once

#include <string>
#include <cstdint>
#include <initializer_list>
#include "ParameterRegistry.hpp"

struct ParamDef {
  uint16_t id;
  const char* name;
  const char* unit;
  float minValue;
  float maxValue;
  float defaultValue;
  const char* smoothing; // "step", "linear", "expo"
};

struct ParamMap {
  const char* nodeType;
  const ParamDef* defs;
  size_t count;
};

inline uint16_t resolveParamIdByName(const ParamMap& map, const std::string& name) {
  std::string n;
  n.reserve(name.size());
  for (char c : name) n.push_back(static_cast<char>(::toupper(c)));
  for (size_t i = 0; i < map.count; ++i) {
    std::string m = map.defs[i].name;
    for (auto& ch : m) ch = static_cast<char>(::toupper(ch));
    if (m == n) return map.defs[i].id;
  }
  return 0;
}

inline const ParamDef* findParamByName(const ParamMap& map, const std::string& name) {
  std::string n;
  n.reserve(name.size());
  for (char c : name) n.push_back(static_cast<char>(::toupper(c)));
  for (size_t i = 0; i < map.count; ++i) {
    std::string m = map.defs[i].name;
    for (auto& ch : m) ch = static_cast<char>(::toupper(ch));
    if (m == n) return &map.defs[i];
  }
  return nullptr;
}

inline float clampToRange(const ParamMap& map, const std::string& name, float value) {
  if (const ParamDef* d = findParamByName(map, name)) {
    if (value < d->minValue) return d->minValue;
    if (value > d->maxValue) return d->maxValue;
  }
  return value;
}

inline float clampToRangeById(const ParamMap& map, uint16_t id, float value) {
  for (size_t i = 0; i < map.count; ++i) {
    if (map.defs[i].id == id) {
      if (value < map.defs[i].minValue) return map.defs[i].minValue;
      if (value > map.defs[i].maxValue) return map.defs[i].maxValue;
      break;
    }
  }
  return value;
}

inline ParameterRegistry<>::Smoothing smoothingForParamId(const ParamMap& map, uint16_t id) {
  for (size_t i = 0; i < map.count; ++i) {
    if (map.defs[i].id == id) {
      const std::string s = map.defs[i].smoothing;
      if (s == "step") return ParameterRegistry<>::Smoothing::Step;
      if (s == "expo") return ParameterRegistry<>::Smoothing::Expo;
      return ParameterRegistry<>::Smoothing::Linear;
    }
  }
  return ParameterRegistry<>::Smoothing::Linear;
}

// Kick
static constexpr ParamDef kKickParams[] = {
  {1, "F0",             "Hz",    40.f, 200.f, 100.f, "linear"},
  {2, "FEND",           "Hz",    20.f, 120.f,  40.f, "linear"},
  {3, "PITCH_DECAY_MS", "ms",    10.f, 200.f,  60.f, "linear"},
  {4, "AMP_DECAY_MS",   "ms",    50.f, 400.f, 200.f, "linear"},
  {5, "GAIN",           "",       0.f,  1.5f,  0.9f, "linear"},
  {6, "CLICK",          "",       0.f,  1.0f,  0.0f, "step"},
  {7, "BPM",            "",       0.f,  300.f,  0.f, "step"},
  {8, "LOOP",           "bool",   0.f,  1.0f,   0.f, "step"},
};

static constexpr ParamMap kKickParamMap{ "kick", kKickParams, sizeof(kKickParams)/sizeof(kKickParams[0]) };

// Clap
static constexpr ParamDef kClapParams[] = {
  {1, "AMP_DECAY_MS", "ms",  20.f, 300.f, 180.f, "linear"},
  {2, "GAIN",         "",     0.f,  1.5f,  0.8f, "linear"},
  {3, "BPM",          "",     0.f,  300.f,  0.f, "step"},
  {4, "LOOP",         "bool", 0.f,  1.0f,   0.f, "step"},
  // Pseudo-parameters for modulation sources (transport/commands can address these)
  {101, "LFO1_FREQ_HZ", "Hz", 0.1f, 100.0f, 4.0f,  "step"},
  {102, "LFO2_FREQ_HZ", "Hz", 0.1f, 100.0f, 0.2f, "step"},
};

static constexpr ParamMap kClapParamMap{ "clap", kClapParams, sizeof(kClapParams)/sizeof(kClapParams[0]) };

// TB-303 Extended
static constexpr ParamDef kTb303Params[] = {
  {1,  "WAVEFORM",         "",    0.f,   1.f,    0.f,   "step"},
  {2,  "TUNE_SEMITONES",   "st", -24.f,  24.f,   0.f,   "linear"},
  {3,  "GLIDE_MS",         "ms",   0.f, 500.f,  10.f,   "linear"},
  {4,  "CUTOFF_HZ",        "Hz",  20.f,18000.f, 800.f,  "linear"},
  {5,  "RESONANCE",        "",     0.f,   1.f,   0.3f,  "linear"},
  {6,  "ENV_MOD",          "",     0.f,   1.f,   0.5f,  "linear"},
  {7,  "FILTER_DECAY_MS",  "ms",  50.f, 800.f, 200.f,  "linear"},
  {8,  "AMP_DECAY_MS",     "ms",  50.f, 800.f, 200.f,  "linear"},
  {9,  "AMP_GAIN",         "",     0.f,   1.5f,  0.8f,  "linear"},
  {10, "NOTE_SEMITONES",   "st",   0.f, 127.f,  48.f,  "step"},
  {11, "VELOCITY",         "",     0.f,   1.f,   1.0f,  "step"},
  {12, "ACCENT",           "",     0.f,   1.f,   0.0f,  "step"},
  // CC pseudo-params (mapped to above):
  {101, "CC1",             "",     0.f,   1.f,   0.0f,  "linear"}, // Mod Wheel → env mod depth
  {102, "CC74",            "",     0.f,   1.f,   0.0f,  "linear"}, // Cutoff (normalized)
  {103, "CC71",            "",     0.f,   1.f,   0.0f,  "linear"}, // Resonance (normalized)
  {104, "CC7",             "",     0.f,   1.f,   0.8f,  "linear"}, // Volume → amp gain
  {105, "PITCH_BEND",      "",   -1.f,   1.f,   0.0f,  "linear"},  // normalized bend
};

static constexpr ParamMap kTb303ParamMap{ "tb303_ext", kTb303Params, sizeof(kTb303Params)/sizeof(kTb303Params[0]) };


