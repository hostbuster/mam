#include "GraphConfig.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include "ParamMap.hpp"
#include <nlohmann/json.hpp>

using nlohmann::json;

static std::string readFileToString(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("Failed to open JSON file: " + path);
  std::ostringstream ss; ss << f.rdbuf();
  return ss.str();
}

GraphSpec loadGraphSpecFromJsonFile(const std::string& path) {
  const std::string text = readFileToString(path);
  json j = json::parse(text);

  GraphSpec spec;
  if (j.contains("version")) spec.version = j.at("version").get<int>();
  if (j.contains("sampleRate")) spec.sampleRate = j.at("sampleRate").get<uint32_t>();
  if (j.contains("channels")) spec.channels = j.at("channels").get<uint32_t>();
  if (j.contains("randomSeed")) spec.randomSeed = j.at("randomSeed").get<uint32_t>();

  if (j.contains("nodes")) {
    for (const auto& n : j.at("nodes")) {
      NodeSpec ns;
      ns.id = n.value("id", "");
      ns.type = n.value("type", "");
      json p = n.value("params", json::object());
      ns.paramsJson = p.dump();
      spec.nodes.push_back(std::move(ns));
    }
  }

  if (j.contains("connections")) {
    for (const auto& c : j.at("connections")) {
      GraphSpec::Connection cc;
      cc.from = c.value("from", "");
      cc.to = c.value("to", "");
      spec.connections.push_back(cc);
    }
  }

  if (j.contains("mixer")) {
    spec.hasMixer = true;
    const auto& m = j.at("mixer");
    spec.mixer.masterPercent = m.value("masterPercent", 100.0f);
    spec.mixer.softClip = m.value("softClip", true);
    if (m.contains("inputs")) {
      for (const auto& inp : m.at("inputs")) {
        GraphSpec::MixerInput mi;
        mi.id = inp.value("id", "");
        mi.gainPercent = inp.value("gainPercent", 100.0f);
        spec.mixer.inputs.push_back(mi);
      }
    }
  }

  // Build nodeId -> type map for param name mapping
  std::unordered_map<std::string, std::string> nodeIdToType;
  for (const auto& ns : spec.nodes) nodeIdToType.emplace(ns.id, ns.type);

  if (j.contains("commands")) {
    for (const auto& c : j.at("commands")) {
      GraphSpec::CommandSpec cs;
      cs.sampleTime = static_cast<uint64_t>(c.value("sampleTime", 0));
      cs.nodeId = c.value("nodeId", "");
      cs.type = c.value("type", "");
      cs.paramName = c.value("param", "");
      cs.paramId = static_cast<uint16_t>(c.value("paramId", 0));
      cs.value = c.value("value", 0.0f);
      cs.rampMs = c.value("rampMs", 0.0f);

      if (cs.paramId == 0 && !cs.paramName.empty()) {
        auto it = nodeIdToType.find(cs.nodeId);
        const std::string nodeType = (it != nodeIdToType.end()) ? it->second : std::string();
        auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
          if (type == "kick") return resolveParamIdByName(kKickParamMap, name);
          if (type == "clap") return resolveParamIdByName(kClapParamMap, name);
          return 0;
        };
        cs.paramId = mapParam(nodeType, cs.paramName);
      }
      spec.commands.push_back(cs);
    }
  }

  if (j.contains("transport")) {
    spec.hasTransport = true;
    const auto& t = j.at("transport");
    spec.transport.bpm = t.value("bpm", 120.0f);
    spec.transport.lengthBars = t.value("lengthBars", 1u);
    spec.transport.resolution = t.value("resolution", 16u);
    spec.transport.swingPercent = t.value("swingPercent", 0.0f);
    if (t.contains("tempoRamps")) {
      for (const auto& tp : t.at("tempoRamps")) {
        GraphSpec::Transport::TempoPoint pnt;
        pnt.bar = tp.value("bar", 0u);
        pnt.bpm = tp.value("bpm", spec.transport.bpm);
        spec.transport.tempoRamps.push_back(pnt);
      }
    }
    if (t.contains("patterns")) {
      for (const auto& p : t.at("patterns")) {
        GraphSpec::TransportPattern tp;
        tp.nodeId = p.value("nodeId", "");
        tp.steps = p.value("steps", "");
        if (p.contains("locks")) {
          for (const auto& lk : p.at("locks")) {
            GraphSpec::TransportLock L;
            L.step = lk.value("step", 0u);
            L.paramName = lk.value("param", "");
            L.paramId = static_cast<uint16_t>(lk.value("paramId", 0));
            L.value = lk.value("value", 0.0f);
            L.rampMs = lk.value("rampMs", 0.0f);
            tp.locks.push_back(L);
          }
        }
        spec.transport.patterns.push_back(tp);
      }
    }
  }

  return spec;
}


