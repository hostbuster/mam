#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include "../core/GraphConfig.hpp"
#include "../../third_party/nlohmann/json.hpp"

struct SessionSpec {
  struct RackRef {
    std::string id;
    std::string path;
    int64_t startOffsetFrames = 0;
    float gain = 1.0f;
    bool muted = false;
    bool solo = false;
    // Optional transport overrides per rack
    uint32_t bars = 0;           // force bars (0 = use graph)
    uint32_t loopCount = 0;      // repeat bars N times
    double loopMinutes = 0.0;    // or target minutes
    double loopSeconds = 0.0;    // or target seconds
    double tailMs = 0.0;         // extra tail for this rack
  };
  struct InsertRef { std::string type; std::string id; nlohmann::json params; std::vector<std::pair<std::string,std::string>> sidechains; };
  struct BusRef { std::string id; uint32_t channels = 2; std::vector<InsertRef> inserts; };
  struct RouteRef { std::string from; std::string to; float gain = 1.0f; };
  struct XfaderRef {
    std::string id;
    std::vector<std::string> racks; // typically size 2
    std::string law = "equal_power"; // "equal_power" | "linear"
    double smoothingMs = 10.0; // slew for x changes
    struct Lfo { std::string wave = "sine"; float freqHz = 0.25f; float phase01 = 0.0f; bool has = false; } lfo;
  };
  struct SessCommand {
    // Absolute time addressing (existing)
    double timeSec = 0.0;
    // Musical time addressing (new)
    std::string rack;           // reference rack for musical time
    uint32_t bar = 0;           // bar number (1-based)
    uint32_t step = 0;          // step within bar (1-based, optional)
    uint32_t res = 16;          // resolution (steps per bar, default 16)
    // Command details
    std::string nodeId;
    std::string type;
    float value = 0.0f;
    float rampMs = 0.0f;
  };
  uint32_t sampleRate = 48000;
  uint32_t channels = 2;
  double durationSec = 0.0; // 0 = auto
  bool loop = false;
  bool alignTransports = false; // align first transport trigger across racks
  std::vector<RackRef> racks;
  std::vector<BusRef> buses;
  std::vector<RouteRef> routes;
  std::vector<XfaderRef> xfaders;
  std::vector<SessCommand> commands;
};

inline SessionSpec loadSessionSpecFromJsonFile(const std::string& path) {
  std::ifstream f(path);
  if (!f) throw std::runtime_error("Failed to open session file: " + path);
  std::stringstream ss; ss << f.rdbuf();
  auto j = nlohmann::json::parse(ss.str());
  SessionSpec s;
  if (j.contains("sampleRate")) s.sampleRate = j["sampleRate"].get<uint32_t>();
  if (j.contains("channels")) s.channels = j["channels"].get<uint32_t>();
  if (j.contains("durationSec")) s.durationSec = j["durationSec"].get<double>();
  if (j.contains("loop")) s.loop = j["loop"].get<bool>();
  if (j.contains("alignTransports")) s.alignTransports = j["alignTransports"].get<bool>();
  if (j.contains("racks")) {
    for (const auto& r : j["racks"]) {
      SessionSpec::RackRef rr;
      rr.id = r.value("id", std::string());
      rr.path = r.value("path", std::string());
      rr.startOffsetFrames = r.value("startOffsetFrames", 0);
      rr.gain = r.value("gain", 1.0f);
      rr.muted = r.value("muted", false);
      rr.solo = r.value("solo", false);
      rr.bars = r.value("bars", 0u);
      rr.loopCount = r.value("loopCount", 0u);
      rr.loopMinutes = r.value("loopMinutes", 0.0);
      rr.loopSeconds = r.value("loopSeconds", 0.0);
      rr.tailMs = r.value("tailMs", 0.0);
      if (rr.id.empty() || rr.path.empty()) throw std::runtime_error("Session rack requires id and path");
      s.racks.push_back(rr);
    }
  }
  if (j.contains("buses")) {
    for (const auto& b : j["buses"]) {
      SessionSpec::BusRef br; br.id = b.value("id", std::string()); br.channels = b.value("channels", 2u);
      if (b.contains("inserts")) {
        for (const auto& ins : b["inserts"]) {
          SessionSpec::InsertRef ir; ir.type = ins.value("type", std::string()); ir.id = ins.value("id", std::string());
          if (ins.contains("params")) ir.params = ins["params"]; if (ins.contains("sidechains")) {
            for (const auto& sc : ins["sidechains"]) {
              const std::string scId = sc.value("id", std::string()); const std::string from = sc.value("from", std::string());
              ir.sidechains.emplace_back(scId, from);
            }
          }
          br.inserts.push_back(std::move(ir));
        }
      }
      if (br.id.empty()) throw std::runtime_error("Session bus requires id");
      s.buses.push_back(std::move(br));
    }
  }
  if (j.contains("routes")) {
    for (const auto& r : j["routes"]) {
      SessionSpec::RouteRef rr; rr.from = r.value("from", std::string()); rr.to = r.value("to", std::string()); rr.gain = r.value("gain", 1.0f);
      if (rr.from.empty() || rr.to.empty()) throw std::runtime_error("Session route requires from and to");
      s.routes.push_back(rr);
    }
  }
  if (j.contains("xfaders")) {
    for (const auto& xj : j["xfaders"]) {
      SessionSpec::XfaderRef xr;
      xr.id = xj.value("id", std::string());
      if (xj.contains("racks") && xj["racks"].is_array()) {
        for (const auto& rr : xj["racks"]) xr.racks.push_back(rr.get<std::string>());
      }
      xr.law = xj.value("law", std::string("equal_power"));
      xr.smoothingMs = xj.value("smoothingMs", 10.0);
      if (xj.contains("lfo")) {
        try {
          xr.lfo.has = true;
          auto lj = xj["lfo"];
          xr.lfo.wave = lj.value("wave", std::string("sine"));
          xr.lfo.freqHz = lj.value("freqHz", 0.25f);
          xr.lfo.phase01 = lj.value("phase01", 0.0f);
        } catch (...) { xr.lfo.has = false; }
      }
      if (xr.id.empty() || xr.racks.empty()) throw std::runtime_error("Session xfader requires id and racks");
      s.xfaders.push_back(std::move(xr));
    }
  }
  if (j.contains("commands")) {
    for (const auto& cj : j["commands"]) {
      SessionSpec::SessCommand sc;
      // Absolute time addressing (existing)
      sc.timeSec = cj.value("timeSec", 0.0);
      // Musical time addressing (new)
      sc.rack = cj.value("rack", std::string());
      sc.bar = cj.value("bar", 0u);
      sc.step = cj.value("step", 0u);
      sc.res = cj.value("res", 16u);
      // Command details
      sc.nodeId = cj.value("nodeId", std::string());
      sc.type = cj.value("type", std::string("SetParam"));
      sc.value = cj.value("value", 0.0f);
      sc.rampMs = cj.value("rampMs", 0.0f);
      if (sc.nodeId.empty()) throw std::runtime_error("Session command requires nodeId");
      s.commands.push_back(std::move(sc));
    }
  }
  return s;
}


