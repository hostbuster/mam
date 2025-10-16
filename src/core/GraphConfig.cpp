#include "GraphConfig.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <cstdlib>
#include <filesystem>
#include "ParamMap.hpp"
#include <nlohmann/json.hpp>

using nlohmann::json;

static std::string readFileToString(const std::string& path) {
  auto tryRead = [](const std::string& p, std::string& out) -> bool {
    std::ifstream f(p);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
  };

  // Fast path: as-is
  std::string text;
  if (tryRead(path, text)) return text;

  // If absolute and failed, bail
  std::error_code ec;
  if (std::filesystem::path(path).is_absolute()) {
    throw std::runtime_error("Failed to open JSON file: " + path);
  }

  // Build search roots: CWD, examples/rack, env MAM_SEARCH_PATHS (colon-separated)
  std::vector<std::string> roots;
  roots.emplace_back("");
  roots.emplace_back("examples/rack/");
  if (const char* env = std::getenv("MAM_SEARCH_PATHS")) {
    std::string s(env);
    size_t start = 0; while (start <= s.size()) {
      size_t sep = s.find(':', start);
      std::string tok = (sep == std::string::npos) ? s.substr(start) : s.substr(start, sep - start);
      if (!tok.empty()) {
        if (tok.back() != '/') tok.push_back('/');
        roots.push_back(tok);
      }
      if (sep == std::string::npos) break; else start = sep + 1;
    }
  }
  for (const auto& r : roots) {
    const std::string candidate = r.empty() ? path : (r + path);
    if (tryRead(candidate, text)) return text;
  }
  throw std::runtime_error("Failed to open JSON file: " + path);
}

GraphSpec loadGraphSpecFromJsonFile(const std::string& path) {
  const std::string text = readFileToString(path);
  json j = json::parse(text);
  // Discriminator: kind (prefer 'rack'; accept legacy 'graph')
  if (j.contains("kind")) {
    const std::string k = j.at("kind").get<std::string>();
    if (k == std::string("rack")) {
      // ok
    } else if (k == std::string("graph")) {
      std::fprintf(stderr, "Warning: kind=graph is deprecated; use kind=rack (%s)\n", path.c_str());
    } else {
      throw std::runtime_error("JSON kind mismatch: expected 'rack' but got '" + k + "' in " + path);
    }
  } else {
    std::fprintf(stderr, "Warning: rack JSON missing 'kind'; defaulting to legacy graph (%s)\n", path.c_str());
  }

  GraphSpec spec;
  if (j.contains("description")) spec.description = j.at("description").get<std::string>();
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
      if (n.contains("ports")) {
        const auto& pr = n.at("ports");
        if (pr.contains("inputs")) {
          for (const auto& ip : pr.at("inputs")) {
            NodeSpec::PortDesc d;
            d.index = ip.value("index", 0u);
            d.name = ip.value("name", "");
            d.type = ip.value("type", "");
            d.channels = ip.value("channels", 0u);
            d.role = ip.value("role", "");
            ns.ports.inputs.push_back(d);
          }
        }
        if (pr.contains("outputs")) {
          for (const auto& op : pr.at("outputs")) {
            NodeSpec::PortDesc d;
            d.index = op.value("index", 0u);
            d.name = op.value("name", "");
            d.type = op.value("type", "");
            d.channels = op.value("channels", 0u);
            d.role = op.value("role", "");
            ns.ports.outputs.push_back(d);
          }
        }
        ns.ports.has = true;
      }
      // Optional modulation spec per node
      if (n.contains("mod")) {
        const auto& md = n.at("mod");
        if (md.contains("lfos")) {
          for (const auto& lj : md.at("lfos")) {
            NodeSpec::ModLfoSpec ls{};
            ls.id = static_cast<uint16_t>(lj.value("id", 0));
            ls.wave = lj.value("wave", std::string("sine"));
            ls.freqHz = lj.value("freqHz", 0.5f);
            ls.phase01 = lj.value("phase", 0.0f);
            ns.mod.lfos.push_back(ls);
          }
        }
        if (md.contains("routes")) {
          for (const auto& rj : md.at("routes")) {
            NodeSpec::ModRouteSpec rs{};
            rs.sourceId = static_cast<uint16_t>(rj.value("sourceId", 0));
            rs.destParamId = static_cast<uint16_t>(rj.value("destParamId", 0));
            rs.destParamName = rj.value("destParam", std::string());
            rs.depth = rj.value("depth", 0.0f);
            rs.offset = rj.value("offset", 0.0f);
            rs.map = rj.value("map", std::string());
            rs.minValue = rj.value("min", 0.0f);
            rs.maxValue = rj.value("max", 0.0f);
            ns.mod.routes.push_back(rs);
          }
        }
        ns.mod.has = true;
      }
      spec.nodes.push_back(std::move(ns));
    }
  }

  if (j.contains("connections")) {
    for (const auto& c : j.at("connections")) {
      GraphSpec::Connection cc;
      cc.from = c.value("from", "");
      cc.to = c.value("to", "");
      cc.gainPercent = c.value("gainPercent", 100.0f);
      cc.dryPercent = c.value("dryPercent", 0.0f);
      cc.fromPort = c.value("fromPort", 0u);
      cc.toPort = c.value("toPort", 0u);
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
          if (type == "tb303_ext") return resolveParamIdByName(kTb303ParamMap, name);
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
    spec.transport.swingExponent = t.value("swingExponent", 1.0f);
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
        if (p.contains("stepsBars")) {
          for (const auto& sb : p.at("stepsBars")) tp.stepsBars.push_back(sb.get<std::string>());
        }
        tp.resolution = p.value("resolution", 0u);
        tp.lengthBars = p.value("lengthBars", 0u);
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


