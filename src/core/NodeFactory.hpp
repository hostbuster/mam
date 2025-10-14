#pragma once

#include <memory>
#include <string>
#include "GraphConfig.hpp"
#include "../instruments/kick/KickFactory.hpp"
#include "../instruments/clap/ClapFactory.hpp"
#include "TransportNode.hpp"
#include "DelayNode.hpp"
#include "MeterNode.hpp"
#include "CompressorNode.hpp"
#include "ReverbNode.hpp"
#include "WiretapNode.hpp"
#include "SpectralDuckerNode.hpp"
#include "../instruments/tb303/Tb303ExtNode.hpp"
#include <type_traits>
// Mixer is not created via NodeFactory; it is set on Graph from GraphSpec.mixer

inline std::unique_ptr<Node> createNodeFromSpec(const NodeSpec& spec) {
  if (spec.type == "kick") {
    auto node = makeKickNodeFromParamsJson(spec.paramsJson);
    // Apply optional modulation spec
    if (node && spec.mod.has) {
      // Configure LFOs/routes via public hooks
      for (const auto& l : spec.mod.lfos) {
        ModLfo::Wave w = ModLfo::Wave::Sine;
        if (l.wave == "triangle") w = ModLfo::Wave::Triangle;
        else if (l.wave == "saw") w = ModLfo::Wave::Saw;
        else if (l.wave == "square") w = ModLfo::Wave::Square;
        node->addLfo(l.id, w, l.freqHz, l.phase01);
      }
      for (const auto& r : spec.mod.routes) {
        // Support LFO.<id>.freqHz target
        if (!r.destParamName.empty() && r.destParamName.rfind("LFO.", 0) == 0 && r.destParamName.find(".freqHz") != std::string::npos) {
          const auto dotPos = r.destParamName.find('.');
          const auto secondDot = r.destParamName.find('.', dotPos + 1);
          const std::string idStr = r.destParamName.substr(dotPos + 1, secondDot - (dotPos + 1));
          const uint16_t lfoId = static_cast<uint16_t>(std::atoi(idStr.c_str()));
          node->addLfoFreqRoute(r.sourceId, lfoId, r.depth, r.offset);
          continue;
        }
        // Support LFO.<id>.phase target
        if (!r.destParamName.empty() && r.destParamName.rfind("LFO.", 0) == 0 && r.destParamName.find(".phase") != std::string::npos) {
          const auto dotPos = r.destParamName.find('.');
          const auto secondDot = r.destParamName.find('.', dotPos + 1);
          const std::string idStr = r.destParamName.substr(dotPos + 1, secondDot - (dotPos + 1));
          const uint16_t lfoId = static_cast<uint16_t>(std::atoi(idStr.c_str()));
          node->addLfoFreqRoute(r.sourceId, lfoId, r.depth, r.offset);
          continue;
        }
        uint16_t dest = r.destParamId;
        if (dest == 0 && !r.destParamName.empty()) dest = resolveParamIdByName(kKickParamMap, r.destParamName);
        if (dest != 0) node->addRoute(r.sourceId, dest, r.depth, r.offset);
      }
    }
    return node;
  }
  if (spec.type == "clap") {
    auto node = makeClapNodeFromParamsJson(spec.paramsJson);
    if (node && spec.mod.has) {
      for (const auto& l : spec.mod.lfos) {
        ModLfo::Wave w = ModLfo::Wave::Sine;
        if (l.wave == "triangle") w = ModLfo::Wave::Triangle;
        else if (l.wave == "saw") w = ModLfo::Wave::Saw;
        else if (l.wave == "square") w = ModLfo::Wave::Square;
        node->addLfo(l.id, w, l.freqHz, l.phase01);
      }
      for (const auto& r : spec.mod.routes) {
        if (!r.destParamName.empty() && r.destParamName.rfind("LFO.", 0) == 0 && r.destParamName.find(".freqHz") != std::string::npos) {
          const auto dotPos = r.destParamName.find('.');
          const auto secondDot = r.destParamName.find('.', dotPos + 1);
          const std::string idStr = r.destParamName.substr(dotPos + 1, secondDot - (dotPos + 1));
          const uint16_t lfoId = static_cast<uint16_t>(std::atoi(idStr.c_str()));
          node->addLfoFreqRoute(r.sourceId, lfoId, r.depth, r.offset);
          continue;
        }
        uint16_t dest = r.destParamId;
        if (dest == 0 && !r.destParamName.empty()) dest = resolveParamIdByName(kClapParamMap, r.destParamName);
        if (dest != 0) {
          if (!r.map.empty() || (r.minValue < r.maxValue)) {
            auto mapMode = ModMatrix<>::Route::Map::Linear;
            if (r.map == "exp") mapMode = ModMatrix<>::Route::Map::Exp;
            const float minV = r.minValue;
            const float maxV = r.maxValue;
            if (minV < maxV) node->addRouteWithRange(r.sourceId, dest, minV, maxV, mapMode);
            else node->addRoute(r.sourceId, dest, r.depth, r.offset);
          } else {
            node->addRoute(r.sourceId, dest, r.depth, r.offset);
          }
        }
      }
    }
    return node;
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
  if (spec.type == "compressor") {
    auto c = std::make_unique<CompressorNode>();
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      c->thresholdDb = static_cast<float>(j.value("thresholdDb", -2.0));
      c->ratio = static_cast<float>(j.value("ratio", 1.2));
      c->attackMs = static_cast<float>(j.value("attackMs", 25.0));
      c->releaseMs = static_cast<float>(j.value("releaseMs", 200.0));
      c->makeupDb = static_cast<float>(j.value("makeupDb", 0.0));
    } catch (...) {}
    return c;
  }
  if (spec.type == "spectral_ducker") {
    auto s = std::make_unique<SpectralDuckerNode>();
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      s->thresholdDb = static_cast<float>(j.value("thresholdDb", -12.0));
      s->ratio = static_cast<float>(j.value("ratio", 2.0));
      s->attackMs = static_cast<float>(j.value("attackMs", 4.0));
      s->releaseMs = static_cast<float>(j.value("releaseMs", 180.0));
      s->makeupDb = static_cast<float>(j.value("makeupDb", 0.0));
      s->lookaheadMs = static_cast<float>(j.value("lookaheadMs", 5.0));
      s->mix = static_cast<float>(j.value("mix", 1.0));
      if (j.contains("bands")) {
        s->bands.clear();
        for (const auto& b : j.at("bands")) {
          SpectralDuckerNode::Band B{};
          B.centerHz = static_cast<float>(b.value("centerHz", 100.0));
          B.q = static_cast<float>(b.value("q", 1.0));
          B.depthDb = static_cast<float>(b.value("depthDb", -6.0));
          s->bands.push_back(B);
        }
      }
    } catch (...) {}
    return s;
  }
  if (spec.type == "reverb") {
    auto r = std::make_unique<ReverbNode>();
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      r->roomSize = static_cast<float>(j.value("roomSize", 0.5));
      r->damp = static_cast<float>(j.value("damp", 0.3));
      r->mix = static_cast<float>(j.value("mix", 0.2));
    } catch (...) {}
    return r;
  }
  if (spec.type == "wiretap") {
    std::string path = "wiretap.wav";
    bool enabled = true;
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      if (j.contains("path")) path = j.value("path", path);
      enabled = j.value("enabled", true);
    } catch (...) {}
    return std::make_unique<WiretapNode>(path, enabled);
  }
  if (spec.type == "tb303_ext") {
    Tb303ExtParams p{};
    try {
      nlohmann::json j = nlohmann::json::parse(spec.paramsJson);
      p.waveform = j.value("waveform", 0);
      p.tuneSemitones = static_cast<float>(j.value("tune", 0.0));
      p.glideMs = static_cast<float>(j.value("glideMs", 10.0));
      p.cutoffHz = static_cast<float>(j.value("cutoff", 800.0));
      p.resonance = static_cast<float>(j.value("resonance", 0.3));
      p.envMod = static_cast<float>(j.value("envMod", 0.5));
      p.filterDecayMs = static_cast<float>(j.value("decay", 200.0));
      p.ampDecayMs = static_cast<float>(j.value("ampDecayMs", 200.0));
      p.ampGain = static_cast<float>(j.value("gain", 0.8));
    } catch (...) {}
    auto node = std::make_unique<Tb303ExtNode>(p);
    if (spec.mod.has) {
      for (const auto& l : spec.mod.lfos) {
        ModLfo::Wave w = ModLfo::Wave::Sine;
        if (l.wave == "triangle") w = ModLfo::Wave::Triangle; else if (l.wave == "saw") w = ModLfo::Wave::Saw; else if (l.wave == "square") w = ModLfo::Wave::Square;
        node->addLfo(l.id, w, l.freqHz, l.phase01);
      }
      for (const auto& r : spec.mod.routes) {
        if (!r.destParamName.empty() && r.destParamName.rfind("LFO.", 0) == 0 && r.destParamName.find(".freqHz") != std::string::npos) {
          const auto dotPos = r.destParamName.find('.');
          const auto secondDot = r.destParamName.find('.', dotPos + 1);
          const std::string idStr = r.destParamName.substr(dotPos + 1, secondDot - (dotPos + 1));
          const uint16_t lfoId = static_cast<uint16_t>(std::atoi(idStr.c_str()));
          node->addLfoFreqRoute(r.sourceId, lfoId, r.depth, r.offset);
          continue;
        }
        if (!r.destParamName.empty() && r.destParamName.rfind("LFO.", 0) == 0 && r.destParamName.find(".phase") != std::string::npos) {
          const auto dotPos = r.destParamName.find('.');
          const auto secondDot = r.destParamName.find('.', dotPos + 1);
          const std::string idStr = r.destParamName.substr(dotPos + 1, secondDot - (dotPos + 1));
          const uint16_t lfoId = static_cast<uint16_t>(std::atoi(idStr.c_str()));
          node->addLfoFreqRoute(r.sourceId, lfoId, r.depth, r.offset);
          continue;
        }
        uint16_t dest = r.destParamId;
        if (dest == 0 && !r.destParamName.empty()) dest = resolveParamIdByName(kTb303ParamMap, r.destParamName);
        if (dest != 0) {
          if (!r.map.empty() || (r.minValue < r.maxValue)) {
            auto mapMode = ModMatrix<>::Route::Map::Linear; if (r.map == "exp") mapMode = ModMatrix<>::Route::Map::Exp;
            const float minV = r.minValue, maxV = r.maxValue;
            if (minV < maxV) node->addRouteWithRange(r.sourceId, dest, minV, maxV, mapMode); else node->addRoute(r.sourceId, dest, r.depth, r.offset);
          } else node->addRoute(r.sourceId, dest, r.depth, r.offset);
        }
      }
    }
    return node;
  }
  // Unknown node type
  std::fprintf(stderr, "Warning: Unknown node type '%s' (id='%s')\n", spec.type.c_str(), spec.id.c_str());
  return nullptr;
}


