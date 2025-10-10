#include "GraphConfig.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
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

  return spec;
}


