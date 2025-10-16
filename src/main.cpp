#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <csignal>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <queue>
#include <memory>
#include <iostream>
#include <unistd.h>
#include <sys/select.h>
#include <cassert>
#include <mutex>
#include <sstream>
#include "instruments/kick/KickSynth.hpp"
#include "offline/OfflineRenderer.hpp"
#include "offline/OfflineGraphRenderer.hpp"
#include "offline/OfflineParallelGraphRenderer.hpp"
#include "session/SessionSpec.hpp"
#include "session/SessionRuntime.hpp"
#include "offline/OfflineTimelineRenderer.hpp"
#include "offline/TransportGenerator.hpp"
#include "io/AudioFileWriter.hpp"
#include "offline/OfflineProgress.hpp"
#include "realtime/RealtimeRenderer.hpp"
#include "realtime/RealtimeGraphRenderer.hpp"
#include "realtime/RealtimeSessionRenderer.hpp"
#include <fstream>
#include "core/Graph.hpp"
#include "core/GraphConfig.hpp"
#include "core/NodeFactory.hpp"
#include "core/MixerNode.hpp"
#include "instruments/kick/KickNode.hpp"
#include "core/ParamMap.hpp"
#include "core/Random.hpp"
#include "core/GraphUtils.hpp"
#include "core/SchemaValidate.hpp"

// Use KickSynth (from dsp/) for both realtime and offline paths

// Realtime callback now lives in RealtimeRenderer

static std::atomic<bool> gRunning{true};

static void onSigInt(int) {
  gRunning.store(false);
}

// non-blocking stdin check using select
static bool isStdinReady() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);
  timeval tv{0, 0};
  int rv = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
  return (rv > 0) && FD_ISSET(STDIN_FILENO, &readfds);
}

// printTopoOrderFromSpec and computeGraphPrerollSamples moved to core/GraphUtils.hpp

static std::string formatDuration(double seconds) {
  if (seconds < 0.0) seconds = 0.0;
  const int64_t totalMs = static_cast<int64_t>(seconds * 1000.0 + 0.5);
  const int64_t hrs = totalMs / (3600 * 1000);
  const int64_t rem1 = totalMs % (3600 * 1000);
  const int64_t mins = rem1 / (60 * 1000);
  const int64_t rem2 = rem1 % (60 * 1000);
  const int64_t secs = rem2 / 1000;
  const int64_t ms = rem2 % 1000;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld.%03lld",
                static_cast<long long>(hrs), static_cast<long long>(mins),
                static_cast<long long>(secs), static_cast<long long>(ms));
  return std::string(buf);
}

static void computePeakAndRms(const std::vector<float>& interleaved, uint32_t /*channels*/, double& outPeakDb, double& outRmsDb) {
  double peak = 0.0;
  long double sumSq = 0.0L;
  const size_t n = interleaved.size();
  for (size_t i = 0; i < n; ++i) {
    const double s = static_cast<double>(interleaved[i]);
    const double a = std::fabs(s);
    if (a > peak) peak = a;
    sumSq += static_cast<long double>(s * s);
  }
  const double rms = (n > 0) ? std::sqrt(static_cast<double>(sumSq / static_cast<long double>(n))) : 0.0;
  auto toDb = [](double x) -> double { return (x > 0.0) ? (20.0 * std::log10(x)) : -std::numeric_limits<double>::infinity(); };
  outPeakDb = toDb(peak);
  outRmsDb = toDb(rms);
}

static const char* toStr(FileFormat f) {
  switch (f) {
    case FileFormat::Wav: return "wav";
    case FileFormat::Aiff: return "aiff";
    case FileFormat::Caf: return "caf";
  }
  return "wav";
}

static const char* toStr(BitDepth d) {
  switch (d) {
    case BitDepth::Pcm16: return "16";
    case BitDepth::Pcm24: return "24";
    case BitDepth::Float32: return "32f";
  }
  return "32f";
}

static void printUsage(const char* exe) {
  std::fprintf(stderr,
               "Usage: %s [--f0 Hz] [--fend Hz] [--pitch-decay ms] [--amp-decay ms]\n"
               "          [--gain 0..1] [--bpm N] [--click 0..1]\n"
               "          [--wav path] [--sr Hz] [--pcm16] [--format wav|aiff|caf] [--bitdepth 16|24|32f] [--offline-threads N]\n"
               "          [--graph path.json] [--quit-after sec]\\\n\n"
               "\nOffline export controls (auto-duration by default):\n"
               "  --duration SEC     Hard duration (overrides auto)\n"
               "  --bars N           Force N bars from transport (if present)\n"
               "  --loop-count N     Repeat transport sequence N times (default 1)\n"
               "  --tail-ms MS       Decay tail appended (default 250)\n"
               "  --normalize        Normalize to peak target (default -1.0 dBFS unless changed)\n"
               "  --peak-target dB   Peak target for normalization (e.g., -0.3)\n"
               "  --verbose          Print realtime loop diagnostics (loop counter and elapsed time)\n"
               "  --print-triggers   Print realtime event deliveries (set/ramps/triggers) at render-time\n"
               "  --dump-events      Print synthesized command events (time/bar/step) before playback/export\n"
               "  --meters           Realtime: print per-rack meters periodically; Offline: print mix meters after export\n"
               "  --meters-interval S  Realtime meters print interval in seconds (min 0.05, default 1.0)\n"
               "  --meters-per-node  Print per-node peak/RMS after run/export (realtime at loop boundaries; offline after export)\n"
               "                      Realtime also prints per-bus meters when buses are defined in session.\n"
               "  --cpu-stats        Print block CPU avg/max and xrun count at end\n"
               "  --cpu-stats-per-node  Print per-node avg/max us at end\n"
               "  --rt-debug-feed   Debug realtime feeder (queue pushes, offsets)\n"
               "  --rt-debug-session Debug realtime session (initial/feeder enqueues)\n"
               "  --session path.json  Run a multi-rack session (realtime or offline when combined with --wav)\n"
               "\nDiagram export (Mermaid):\n"
               "  --export-mermaid-session path.json   Print session (racks/buses/routes) as Mermaid flowchart to stdout\n"
               "  --export-mermaid-graph path.json     Print graph (nodes/connections) as Mermaid flowchart to stdout\n"
               "  --loop-minutes M   Repeat transport to reach at least M minutes (offline)\n"
               "  --loop-seconds S   Repeat transport to reach at least S seconds (offline)\n"
               "  --random-seed N    Override JSON randomSeed for deterministic randomness (0 to skip)\n"
               "          [--validate path.json] [--list-nodes path.json] [--list-params kick|clap] [--list-node-types]\n"
               "\nProgress (offline):\n"
               "  --progress-ms N    Progress print interval in ms (0=disable)\n"
               "  --no-progress      Disable progress prints\n"
               "  --no-summary       Disable final speedup summary\n"
               "\n"
               "Examples:\n"
               "  mam                       # one-shot, defaults (real-time)\n"
               "  mam --bpm 120            # 120 BPM continuous till Ctrl-C (real-time)\n"
               "  mam --graph demo.json --wav demo.wav         # export using auto-duration\n",
               exe);
}
// Generate a simple Mermaid flowchart for a SessionSpec (racks/buses/routes)
static std::string generateMermaidForSession(const SessionSpec& s) {
  std::ostringstream out;
  out << "flowchart LR\n";
  // Declare rack nodes
  for (const auto& r : s.racks) {
    out << "  " << r.id << "[\"Rack " << r.id << "\"]\n";
  }
  // Declare bus nodes (if any)
  for (const auto& b : s.buses) {
    out << "  " << b.id << "[(\"Bus " << b.id << "\")]\n";
  }
  // Declare master mix sink
  out << "  Mix[(\"Master Mix\")]\n";
  // Routes from racks to buses
  std::unordered_set<std::string> racksWithRoute;
  for (const auto& rt : s.routes) {
    racksWithRoute.insert(rt.from);
    out << "  " << rt.from << " -- \"gain=" << rt.gain << "\" --> " << rt.to << "\n";
  }
  // Any rack without a route goes directly to Mix
  for (const auto& r : s.racks) {
    if (racksWithRoute.find(r.id) == racksWithRoute.end()) {
      out << "  " << r.id << " --> Mix\n";
    }
  }
  // Buses go to Mix
  for (const auto& b : s.buses) {
    out << "  " << b.id << " --> Mix\n";
  }
  return out.str();
}

// Generate a simple Mermaid flowchart for a GraphSpec (nodes/connections)
static std::string generateMermaidForGraph(const GraphSpec& g) {
  std::ostringstream out;
  out << "flowchart LR\n";
  // Nodes
  for (const auto& n : g.nodes) {
    out << "  " << n.id << "[\"" << n.type << ": " << n.id << "\"]\n";
  }
  // Connections
  for (const auto& c : g.connections) {
    out << "  " << c.from << " -- \"g=" << c.gainPercent;
    if (c.fromPort != 0 || c.toPort != 0) {
      out << ", p" << c.fromPort << "->" << c.toPort;
    }
    if (c.dryPercent > 0.0f) {
      out << ", dry=" << c.dryPercent;
    }
    out << "\" --> " << c.to << "\n";
  }
  // Mixer (if any) — show inputs to Mix
  if (g.hasMixer) {
    out << "  Mix[(\"Graph Mix\")]\n";
    for (const auto& mi : g.mixer.inputs) {
      out << "  " << mi.id << " -- \"g=" << mi.gainPercent << "\" --> Mix\n";
    }
  }
  return out.str();
}


static const char* dupStr(const std::string& s) {
  char* p = static_cast<char*>(std::malloc(s.size() + 1));
  if (!p) return nullptr;
  std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}
// Connectivity advisories (non-fatal): e.g., compressor without sidechain key
static void warnSidechainConnectivity(const GraphSpec& spec) {
  std::unordered_map<std::string, std::string> nodeType;
  nodeType.reserve(spec.nodes.size());
  for (const auto& n : spec.nodes) nodeType.emplace(n.id, n.type);
  std::unordered_map<std::string, bool> compHasKey;
  for (const auto& n : spec.nodes) if (n.type == std::string("compressor")) compHasKey[n.id] = false;
  for (const auto& c : spec.connections) {
    auto itT = nodeType.find(c.to);
    if (itT != nodeType.end() && itT->second == std::string("compressor") && c.toPort == 1u) {
      compHasKey[c.to] = true;
    }
  }
  for (const auto& kv : compHasKey) {
    if (!kv.second) {
      std::fprintf(stderr, "Warning: compressor '%s' has no sidechain key connected (toPort=1); using self-detection.\n", kv.first.c_str());
    }
  }
}


// Intern nodeId strings to avoid per-command heap churn/leaks in realtime enqueue paths
static const char* internNodeId(const std::string& s) {
  static std::unordered_map<std::string, const char*> pool;
  static std::mutex m;
  std::lock_guard<std::mutex> lock(m);
  auto it = pool.find(s);
  if (it != pool.end()) return it->second;
  const char* p = dupStr(s);
  pool.emplace(s, p);
  return p;
}

static int listNodesGraphJson(const std::string& path) {
  try {
    GraphSpec spec = loadGraphSpecFromJsonFile(path);
    std::printf("Nodes (%zu):\n", spec.nodes.size());
    for (const auto& n : spec.nodes) {
      std::printf("- id=%s type=%s\n", n.id.c_str(), n.type.c_str());
    }
    if (spec.hasMixer) {
      std::printf("Mixer: master=%g%% softClip=%s inputs=%zu\n",
                  spec.mixer.masterPercent, spec.mixer.softClip ? "true" : "false", spec.mixer.inputs.size());
    }
    if (spec.hasTransport) {
      std::printf("Transport: bpm=%.2f bars=%u res=%u swing=%.1f%% patterns=%zu ramps=%zu\n",
                  spec.transport.bpm, spec.transport.lengthBars, spec.transport.resolution,
                  spec.transport.swingPercent, spec.transport.patterns.size(), spec.transport.tempoRamps.size());
    }
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
    return 1;
  }
}

static int structuralCheckJson(const std::string& path) {
  int errors = 0;
  try {
    nlohmann::json j;
    {
      std::ifstream f(path);
      if (!f.good()) { std::fprintf(stderr, "Cannot open %s\n", path.c_str()); return 1; }
      f >> j;
    }
    if (!j.is_object()) { std::fprintf(stderr, "%s: top-level must be object\n", path.c_str()); return 2; }
    if (!j.contains("version") || !j["version"].is_number_integer()) {
      std::fprintf(stderr, "Missing/integer 'version'\n"); errors++;
    }
    if (!j.contains("nodes") || !j["nodes"].is_array()) {
      std::fprintf(stderr, "Missing/array 'nodes'\n"); errors++;
    } else {
      for (const auto& n : j["nodes"]) {
        if (!n.is_object()) { std::fprintf(stderr, "Node entry is not object\n"); errors++; continue; }
        if (!n.contains("id") || !n["id"].is_string()) { std::fprintf(stderr, "Node missing string 'id'\n"); errors++; }
        if (!n.contains("type") || !n["type"].is_string()) { std::fprintf(stderr, "Node missing string 'type'\n"); errors++; }
      }
    }
    if (j.contains("connections")) {
      const auto& arr = j["connections"];
      if (!arr.is_array()) { std::fprintf(stderr, "'connections' must be array\n"); errors++; }
      else {
        for (const auto& c : arr) {
          if (!c.is_object()) { std::fprintf(stderr, "Connection entry is not object\n"); errors++; continue; }
          if (!c.contains("from") || !c["from"].is_string()) { std::fprintf(stderr, "Connection missing string 'from'\n"); errors++; }
          if (!c.contains("to") || !c["to"].is_string()) { std::fprintf(stderr, "Connection missing string 'to'\n"); errors++; }
          if (c.contains("gainPercent") && !c["gainPercent"].is_number()) { std::fprintf(stderr, "Connection gainPercent must be number\n"); errors++; }
          if (c.contains("dryPercent") && !c["dryPercent"].is_number()) { std::fprintf(stderr, "Connection dryPercent must be number\n"); errors++; }
          if (c.contains("fromPort") && !c["fromPort"].is_number_integer()) { std::fprintf(stderr, "Connection fromPort must be integer\n"); errors++; }
          if (c.contains("toPort") && !c["toPort"].is_number_integer()) { std::fprintf(stderr, "Connection toPort must be integer\n"); errors++; }
        }
      }
    }
    return (errors == 0) ? 0 : 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Schema structural check failed: %s\n", e.what());
    return 1;
  }
}

// removed legacy schemaEnforceJson (replaced by validateJsonWithDraft2020)

static int validateGraphJson(const std::string& path) {
  try {
    // Structural sanity (schema-aligned, lightweight)
    if (int s = structuralCheckJson(path); s == 1) return 1; else if (s == 2) { /* continue with detailed checks but mark */ }
    // Schema enforcement (best-effort using bundled schema)
    {
      const std::string schemaPath = std::string("docs/schema.graph.v1.json");
      std::string diag;
      int s2 = validateJsonWithDraft2020(path, schemaPath, diag);
      if (s2 == 1) return 1; // fatal
      if (s2 == 2) std::fprintf(stderr, "Schema: %s\n", diag.c_str());
    }
    GraphSpec spec = loadGraphSpecFromJsonFile(path);
    int errors = 0;
    std::vector<std::string> nodeIds;
    nodeIds.reserve(spec.nodes.size());
    for (const auto& n : spec.nodes) nodeIds.push_back(n.id);
    auto hasNode = [&](const std::string& id) {
      return std::find(nodeIds.begin(), nodeIds.end(), id) != nodeIds.end();
    };
    // Check unique node ids
    {
      auto tmp = nodeIds; std::sort(tmp.begin(), tmp.end());
      for (size_t i = 1; i < tmp.size(); ++i) {
        if (tmp[i] == tmp[i-1]) { std::fprintf(stderr, "Duplicate node id: %s\n", tmp[i].c_str()); errors++; }
      }
    }
    // Known node types + minimal transport check
    for (const auto& n : spec.nodes) {
      if (!(n.type == "kick" || n.type == "clap" || n.type == "transport" || n.type == "delay" || n.type == "meter" || n.type == "compressor" || n.type == "reverb")) {
        std::fprintf(stderr, "Unknown node type '%s' (id=%s)\n", n.type.c_str(), n.id.c_str());
      }
      if (n.type == "transport") {
        try {
          nlohmann::json pj = nlohmann::json::parse(n.paramsJson);
          if (pj.contains("pattern")) {
            const auto& p = pj.at("pattern");
            const std::string target = p.value("nodeId", "");
            const std::string steps = p.value("steps", "");
            if (target.empty() || !hasNode(target)) { std::fprintf(stderr, "Transport node '%s' references unknown node '%s'\n", n.id.c_str(), target.c_str()); errors++; }
            if (steps.empty()) { std::fprintf(stderr, "Transport node '%s' has empty steps pattern\n", n.id.c_str()); errors++; }
          }
        } catch (...) {
          std::fprintf(stderr, "Transport node '%s' params parse failed\n", n.id.c_str()); errors++;
        }
      }
    }
    // Mixer inputs exist
    if (spec.hasMixer) {
      // dup check
      {
        std::vector<std::string> ids;
        ids.reserve(spec.mixer.inputs.size());
        for (const auto& mi : spec.mixer.inputs) ids.push_back(mi.id);
        auto tmp = ids; std::sort(tmp.begin(), tmp.end());
        for (size_t i = 1; i < tmp.size(); ++i) if (tmp[i] == tmp[i-1]) {
          std::fprintf(stderr, "Duplicate mixer input '%s'\n", tmp[i].c_str()); errors++;
        }
      }
      for (const auto& mi : spec.mixer.inputs) {
        if (!hasNode(mi.id)) { std::fprintf(stderr, "Mixer references unknown node '%s'\n", mi.id.c_str()); errors++; }
      }
    }
    // Commands
    for (const auto& c : spec.commands) {
      if (!hasNode(c.nodeId)) { std::fprintf(stderr, "Command references unknown node '%s'\n", c.nodeId.c_str()); errors++; continue; }
      if (c.type == "SetParam" || c.type == "SetParamRamp") {
        uint16_t pid = c.paramId;
        if (pid == 0 && !c.paramName.empty()) {
          // resolve by name
          std::string nodeType;
          for (const auto& n : spec.nodes) if (n.id == c.nodeId) { nodeType = n.type; break; }
          if (nodeType == "kick") pid = resolveParamIdByName(kKickParamMap, c.paramName);
          else if (nodeType == "clap") pid = resolveParamIdByName(kClapParamMap, c.paramName);
          else if (nodeType == "tb303_ext") pid = resolveParamIdByName(kTb303ParamMap, c.paramName);
        }
        if (pid == 0) { std::fprintf(stderr, "Command missing/unknown param (node=%s)\n", c.nodeId.c_str()); errors++; }
      }
    }
    // Transport patterns and locks
    if (spec.hasTransport) {
      const uint32_t stepsPerBar = spec.transport.resolution ? spec.transport.resolution : 16u;
      // Build nodeId->type for param name validation
      std::unordered_map<std::string, std::string> nodeIdToType;
      for (const auto& ns : spec.nodes) nodeIdToType.emplace(ns.id, ns.type);
      auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
        if (type == std::string("kick")) return resolveParamIdByName(kKickParamMap, name);
        if (type == std::string("clap")) return resolveParamIdByName(kClapParamMap, name);
        if (type == std::string("tb303_ext")) return resolveParamIdByName(kTb303ParamMap, name);
        return 0;
      };
      for (const auto& p : spec.transport.patterns) {
        if (!hasNode(p.nodeId)) { std::fprintf(stderr, "Pattern references unknown node '%s'\n", p.nodeId.c_str()); errors++; }
        if (p.steps.empty()) { std::fprintf(stderr, "Pattern for node '%s' has empty steps\n", p.nodeId.c_str()); errors++; }
        if (!p.steps.empty() && p.steps.size() != stepsPerBar) {
          std::fprintf(stderr, "Pattern for node '%s' has %zu steps but resolution is %u\n", p.nodeId.c_str(), p.steps.size(), stepsPerBar);
        }
        for (const auto& L : p.locks) {
          if (L.step >= stepsPerBar) { std::fprintf(stderr, "Lock step %u out of range for node '%s' (res=%u)\n", L.step, p.nodeId.c_str(), stepsPerBar); errors++; }
          if (L.paramId == 0 && L.paramName.empty()) { std::fprintf(stderr, "Lock missing param for node '%s'\n", p.nodeId.c_str()); errors++; }
          if (L.paramId == 0 && !L.paramName.empty()) {
            uint16_t pid = 0;
            auto it = nodeIdToType.find(p.nodeId);
            if (it != nodeIdToType.end()) pid = mapParam(it->second, L.paramName);
            if (pid == 0) { std::fprintf(stderr, "Lock has unknown param '%s' for node '%s'\n", L.paramName.c_str(), p.nodeId.c_str()); errors++; }
          }
        }
      }
    }

    // Connections validation and cycle check (Kahn)
    if (!spec.connections.empty()) {
      // endpoints exist
      {
        std::unordered_set<std::string> edgeSet;
        for (const auto& c : spec.connections) {
          const std::string key = c.from + std::string("->") + c.to;
          if (!edgeSet.insert(key).second) {
            std::fprintf(stderr, "Duplicate connection %s\n", key.c_str()); errors++;
          }
        }
      }
      for (const auto& c : spec.connections) {
        if (!hasNode(c.from)) { std::fprintf(stderr, "Connection 'from' unknown node '%s'\n", c.from.c_str()); errors++; }
        if (!hasNode(c.to)) { std::fprintf(stderr, "Connection 'to' unknown node '%s'\n", c.to.c_str()); errors++; }
        if (c.from == c.to) { std::fprintf(stderr, "Connection self-edge not allowed: %s->%s\n", c.from.c_str(), c.to.c_str()); errors++; }
        if (c.gainPercent < 0.0f || c.gainPercent > 200.0f) {
          std::fprintf(stderr, "Connection %s->%s gainPercent out of range: %g\n", c.from.c_str(), c.to.c_str(), c.gainPercent); errors++;
        }
      }
      // cycle detection
      std::unordered_map<std::string,int> indeg;
      for (const auto& n : spec.nodes) indeg[n.id] = 0;
      for (const auto& e : spec.connections) if (indeg.find(e.to)!=indeg.end()) indeg[e.to]++;
      std::unordered_multimap<std::string,std::string> adj;
      for (const auto& e : spec.connections) adj.emplace(e.from, e.to);
      std::vector<std::string> q; q.reserve(indeg.size());
      for (const auto& kv : indeg) if (kv.second==0) q.push_back(kv.first);
      size_t visited = 0;
      for (size_t i = 0; i < q.size(); ++i) {
        const auto u = q[i]; visited++;
        auto range = adj.equal_range(u);
        for (auto it = range.first; it != range.second; ++it) {
          auto& v = it->second; if (--indeg[v]==0) q.push_back(v);
        }
      }
      if (visited != indeg.size()) {
        std::fprintf(stderr, "Connections contain a cycle (visited %zu of %zu)\n", visited, indeg.size());
        errors++;
      }
      // dryPercent range
      for (const auto& c : spec.connections) {
        if (c.dryPercent < 0.0f || c.dryPercent > 200.0f) {
          std::fprintf(stderr, "Connection %s->%s dryPercent out of range: %g\n", c.from.c_str(), c.to.c_str(), c.dryPercent); errors++;
        }
      }
      // warn if dry tap and also mixed directly (double-count risk)
      if (spec.hasMixer) {
        std::unordered_set<std::string> mixed;
        for (const auto& mi : spec.mixer.inputs) mixed.insert(mi.id);
        for (const auto& c : spec.connections) {
          if (c.dryPercent > 0.0f && mixed.count(c.from)) {
            std::fprintf(stderr, "Warning: %s is in mixer inputs and has dryPercent>0 on edge %s->%s (double-count).\n",
                         c.from.c_str(), c.from.c_str(), c.to.c_str());
          }
        }
      }
      // fromPort/toPort validation against declared ports (if provided)
      {
        std::unordered_map<std::string, std::unordered_set<uint32_t>> inPorts;
        std::unordered_map<std::string, std::unordered_set<uint32_t>> outPorts;
        std::unordered_map<std::string, std::unordered_map<uint32_t, std::string>> inTypes;
        std::unordered_map<std::string, std::unordered_map<uint32_t, std::string>> outTypes;
        for (const auto& n : spec.nodes) {
          if (n.ports.has) {
            for (const auto& ip : n.ports.inputs) { inPorts[n.id].insert(ip.index); inTypes[n.id][ip.index] = ip.type; }
            for (const auto& op : n.ports.outputs) { outPorts[n.id].insert(op.index); outTypes[n.id][op.index] = op.type; }
          }
        }
        // Warn if audio port channel count mismatches graph channels (simple MVP policy)
        for (const auto& n : spec.nodes) {
          if (!n.ports.has) continue;
          for (const auto& ip : n.ports.inputs) {
            if (ip.type == std::string("audio") && ip.channels != 0u && ip.channels != spec.channels) {
              std::fprintf(stderr, "Warning: node %s input port %u channels=%u != graph channels=%u (adapter not yet implemented)\n",
                           n.id.c_str(), ip.index, ip.channels, spec.channels);
            }
          }
          for (const auto& op : n.ports.outputs) {
            if (op.type == std::string("audio") && op.channels != 0u && op.channels != spec.channels) {
              std::fprintf(stderr, "Warning: node %s output port %u channels=%u != graph channels=%u (adapter not yet implemented)\n",
                           n.id.c_str(), op.index, op.channels, spec.channels);
            }
          }
        }
        for (const auto& c : spec.connections) {
          if (outPorts.count(c.from) && !outPorts[c.from].count(c.fromPort)) {
            std::fprintf(stderr, "Connection %s->%s references unknown fromPort %u\n", c.from.c_str(), c.to.c_str(), c.fromPort); errors++;
          }
          if (inPorts.count(c.to) && !inPorts[c.to].count(c.toPort)) {
            std::fprintf(stderr, "Connection %s->%s references unknown toPort %u\n", c.from.c_str(), c.to.c_str(), c.toPort); errors++; }
          // Type compatibility (audio->audio only for now)
          if (outTypes.count(c.from) && outTypes[c.from].count(c.fromPort) && inTypes.count(c.to) && inTypes[c.to].count(c.toPort)) {
            const std::string& ft = outTypes[c.from][c.fromPort];
            const std::string& tt = inTypes[c.to][c.toPort];
            if (!(ft == std::string("audio") && tt == std::string("audio"))) {
              std::fprintf(stderr, "Connection %s(%s)->%s(%s) port types incompatible\n", c.from.c_str(), ft.c_str(), c.to.c_str(), tt.c_str()); errors++;
            }
          }
        }
      }
      // fromPort/toPort supported (aggregated) – no warnings
    }

    // Type-specific param sanity checks
    for (const auto& n : spec.nodes) {
      if (n.type == std::string("delay")) {
        try {
          nlohmann::json pj = nlohmann::json::parse(n.paramsJson);
          const double mix = pj.value("mix", 1.0);
          const double fb = pj.value("feedback", 0.0);
          if (mix < 0.0 || mix > 1.0) {
            std::fprintf(stderr, "Delay '%s' mix out of range [0..1]: %g\n", n.id.c_str(), mix); errors++;
          }
          if (fb < 0.0 || fb >= 0.98) {
            std::fprintf(stderr, "Delay '%s' feedback suspicious (>=0.98 may blow up): %g\n", n.id.c_str(), fb);
          }
        } catch (...) {
          std::fprintf(stderr, "Delay '%s' params parse failed\n", n.id.c_str()); errors++;
        }
      } else if (n.type == std::string("meter")) {
        try {
          nlohmann::json pj = nlohmann::json::parse(n.paramsJson);
          const std::string target = pj.value("target", std::string());
          if (target.empty() || !hasNode(target)) {
            std::fprintf(stderr, "Meter '%s' target unknown node '%s'\n", n.id.c_str(), target.c_str()); errors++;
          }
        } catch (...) {
          std::fprintf(stderr, "Meter '%s' params parse failed\n", n.id.c_str()); errors++;
        }
      }
    }
    if (errors == 0) {
      std::printf("%s: OK\n", path.c_str());
      return 0;
    }
    return 2;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
    return 1;
  }
}

// Diagnostics: dump command list with bar/step using provided bpm/resolution/sampleRate
static void dumpCommands(const std::vector<GraphSpec::CommandSpec>& cmds,
                         uint32_t sr,
                         double bpm,
                         uint32_t resolution,
                         const char* tag) {
  const double secPerBeat = (bpm > 0.0) ? (60.0 / bpm) : (60.0 / 120.0);
  const double secPerBar = 4.0 * secPerBeat;
  const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * static_cast<double>(sr) + 0.5);
  for (const auto& c : cmds) {
    const uint64_t st = c.sampleTime;
    const uint64_t barIdx = (framesPerBar > 0) ? (st / framesPerBar) : 0ull;
    const uint64_t withinBar = (framesPerBar > 0) ? (st % framesPerBar) : st;
    const uint64_t stepIdx = (framesPerBar > 0 && resolution > 0)
      ? ((withinBar * static_cast<uint64_t>(resolution) + framesPerBar / 2) / framesPerBar)
      : 0ull;
    const char* typeStr = (c.type == std::string("Trigger")) ? "TRIG"
                          : (c.type == std::string("SetParam")) ? "SET"
                          : (c.type == std::string("SetParamRamp")) ? "RAMP" : "?";
    std::fprintf(stderr, "%s: t=%llu bar=%llu step=%llu node=%s type=%s pid=%u val=%.3f rampMs=%.1f param=%s\n",
                 tag,
                 static_cast<unsigned long long>(st),
                 static_cast<unsigned long long>(barIdx + 1ull),
                 static_cast<unsigned long long>((stepIdx % (resolution ? resolution : 16u)) + 1ull),
                 c.nodeId.c_str(), typeStr, c.paramId, c.value, c.rampMs,
                 c.paramName.empty() ? "" : c.paramName.c_str());
  }
}

int main(int argc, char** argv) {
  KickParams params;
  std::string wavPath;
  FileFormat outFormat = FileFormat::Wav;
  BitDepth outDepth = BitDepth::Float32;
  std::string graphPath;
  std::string sessionPath;
  std::string validatePath;
  std::string listNodesPath;
  std::string listParamsType;
  bool listNodeTypes = false;
  double offlineSr = 48000.0;
  bool pcm16 = false;
  double quitAfterSec = 0.0;
  uint32_t offlineThreads = 0; // 0=auto (fallback to single-thread renderer if 0)
  // Export behavior controls
  double overrideDurationSec = -1.0; // < 0 means auto
  uint32_t overrideBars = 0;         // 0 means use transport length
  uint32_t overrideLoopCount = 0;    // 0 means single pass
  double loopMinutes = 0.0;          // derive loop-count if > 0
  double loopSeconds = 0.0;          // derive loop-count if > 0
  double tailMs = 250.0;             // default decay tail
  bool doNormalize = false;          // normalize to peak target if true
  double peakTargetDb = -1.0;        // default peak target when normalizing
  bool printTopo = false;            // print topo order from connections
  bool printMeters = false;          // print meter summary explicitly
  double metersIntervalSec = 1.0;    // realtime meters print interval
  bool tailOverridden = false;
  bool verbose = false;              // realtime loop diagnostics
  uint32_t randomSeedOverride = 0;   // override JSON randomSeed if non-zero
  bool metersPerNode = false;
  std::string metricsNdjsonPath; bool metricsScopeRacks = true; bool metricsScopeBuses = true; // nodes later
  std::string exportMermaidSessionPath;
  std::string exportMermaidGraphPath;
  bool cpuStats = false;
  bool cpuStatsPerNode = false;
  bool rtDebugFeed = false;
  bool rtDebugSession = false;
  bool printTriggers = false;
  bool dumpEvents = false;
  bool schemaStrict = false;         // enforce JSON Schema on load
  // Startup banner (binary identity)
  {
    static const char* kMamVersion = "0.0.1";
    std::fprintf(stderr, "mam -- version %s starting up (built %s %s)\n", kMamVersion, __DATE__, __TIME__);
  }
  for (int i = 1; i < argc; ++i) {
    const char* a = argv[i];
    auto need = [&](int remain) {
      if (i + remain >= argc) {
        printUsage(argv[0]);
        std::exit(1);
      }
    };
    if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    } else if (std::strcmp(a, "--f0") == 0) {
      need(1); params.startFreqHz = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--fend") == 0) {
      need(1); params.endFreqHz = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--pitch-decay") == 0) {
      need(1); params.pitchDecayMs = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--amp-decay") == 0) {
      need(1); params.ampDecayMs = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--gain") == 0) {
      need(1); params.gain = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--bpm") == 0) {
      need(1); params.bpm = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--duration") == 0) {
      need(1); overrideDurationSec = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--click") == 0) {
      need(1); params.click = static_cast<float>(std::atof(argv[++i]));
    } else if (std::strcmp(a, "--wav") == 0) {
      need(1); wavPath = argv[++i];
    } else if (std::strcmp(a, "--sr") == 0) {
      need(1); offlineSr = std::atof(argv[++i]);
      if (offlineSr <= 8000.0) offlineSr = 8000.0;
    } else if (std::strcmp(a, "--format") == 0) {
      need(1); {
        const char* f = argv[++i];
        if (std::strcmp(f, "wav") == 0) outFormat = FileFormat::Wav;
        else if (std::strcmp(f, "aiff") == 0) outFormat = FileFormat::Aiff;
        else if (std::strcmp(f, "caf") == 0) outFormat = FileFormat::Caf;
      }
    } else if (std::strcmp(a, "--bitdepth") == 0) {
      need(1); {
        const char* b = argv[++i];
        if (std::strcmp(b, "16") == 0) outDepth = BitDepth::Pcm16;
        else if (std::strcmp(b, "24") == 0) outDepth = BitDepth::Pcm24;
        else outDepth = BitDepth::Float32;
      }
    } else if (std::strcmp(a, "--pcm16") == 0) {
      pcm16 = true;
    } else if (std::strcmp(a, "--offline-threads") == 0) {
      need(1); offlineThreads = static_cast<uint32_t>(std::max(0, std::atoi(argv[++i])));
    } else if (std::strcmp(a, "--quit-after") == 0) {
      need(1); quitAfterSec = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--bars") == 0) {
      need(1); overrideBars = static_cast<uint32_t>(std::max(0, std::atoi(argv[++i])));
    } else if (std::strcmp(a, "--loop-count") == 0) {
      need(1); overrideLoopCount = static_cast<uint32_t>(std::max(0, std::atoi(argv[++i])));
    } else if (std::strcmp(a, "--loop-minutes") == 0) {
      need(1); loopMinutes = std::max(0.0, std::atof(argv[++i]));
    } else if (std::strcmp(a, "--loop-seconds") == 0) {
      need(1); loopSeconds = std::max(0.0, std::atof(argv[++i]));
    } else if (std::strcmp(a, "--tail-ms") == 0) {
      need(1); tailMs = std::max(0.0, std::atof(argv[++i])); tailOverridden = true;
    } else if (std::strcmp(a, "--normalize") == 0) {
      doNormalize = true; peakTargetDb = -1.0;
    } else if (std::strcmp(a, "--peak-target") == 0) {
      need(1); doNormalize = true; peakTargetDb = std::atof(argv[++i]);
    } else if (std::strcmp(a, "--verbose") == 0 || std::strcmp(a, "-v") == 0) {
      verbose = true;
    } else if (std::strcmp(a, "--graph") == 0) {
      need(1); graphPath = argv[++i];
    } else if (std::strcmp(a, "--session") == 0) {
      need(1); sessionPath = argv[++i];
    } else if (std::strcmp(a, "--metrics-ndjson") == 0) {
      need(1); metricsNdjsonPath = argv[++i];
    } else if (std::strcmp(a, "--metrics-scope") == 0) {
      need(1); {
        std::string sc = argv[++i];
        metricsScopeRacks = (sc.find("racks") != std::string::npos);
        metricsScopeBuses = (sc.find("buses") != std::string::npos);
      }
    } else if (std::strcmp(a, "--export-mermaid-session") == 0) {
      need(1); exportMermaidSessionPath = argv[++i];
    } else if (std::strcmp(a, "--export-mermaid-graph") == 0) {
      need(1); exportMermaidGraphPath = argv[++i];
    } else if (std::strcmp(a, "--print-topo") == 0) {
      printTopo = true;
    } else if (std::strcmp(a, "--meters") == 0) {
      printMeters = true;
    } else if (std::strcmp(a, "--meters-interval") == 0) {
      need(1);
      metersIntervalSec = std::atof(argv[++i]);
      if (!(metersIntervalSec > 0.05)) metersIntervalSec = 1.0; // clamp to sane default
    } else if (std::strcmp(a, "--meters-per-node") == 0) {
      metersPerNode = true;
    } else if (std::strcmp(a, "--cpu-stats") == 0) {
      cpuStats = true;
    } else if (std::strcmp(a, "--cpu-stats-per-node") == 0) {
      cpuStatsPerNode = true;
    } else if (std::strcmp(a, "--rt-debug-feed") == 0) {
      rtDebugFeed = true;
    } else if (std::strcmp(a, "--rt-debug-session") == 0) {
      rtDebugSession = true;
    } else if (std::strcmp(a, "--random-seed") == 0) {
      need(1); randomSeedOverride = static_cast<uint32_t>(std::max(0, std::atoi(argv[++i])));
    } else if (std::strcmp(a, "--progress-ms") == 0) {
      need(1); gOfflineProgressMs = std::atoi(argv[++i]);
    } else if (std::strcmp(a, "--no-progress") == 0) {
      gOfflineProgressEnabled = false;
    } else if (std::strcmp(a, "--no-summary") == 0) {
      gOfflineSummaryEnabled = false;
    } else if (std::strcmp(a, "--print-triggers") == 0) {
      printTriggers = true;
    } else if (std::strcmp(a, "--dump-events") == 0) {
      dumpEvents = true;
    } else if (std::strcmp(a, "--schema-strict") == 0) {
      schemaStrict = true;
    } else if (std::strcmp(a, "--validate") == 0) {
      need(1); validatePath = argv[++i];
    } else if (std::strcmp(a, "--list-nodes") == 0) {
      need(1); listNodesPath = argv[++i];
    } else if (std::strcmp(a, "--list-params") == 0) {
      need(1); listParamsType = argv[++i];
    } else if (std::strcmp(a, "--list-node-types") == 0) {
      listNodeTypes = true;
    } else {
      std::fprintf(stderr, "Unknown option: %s\n", a);
      printUsage(argv[0]);
      return 1;
    }
  }

  // Diagram export early-outs
  if (!exportMermaidSessionPath.empty()) {
    try {
      SessionSpec sess = loadSessionSpecFromJsonFile(exportMermaidSessionPath);
      const std::string mmd = generateMermaidForSession(sess);
      std::fwrite(mmd.data(), 1, mmd.size(), stdout);
      return 0;
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Diagram export failed: %s\n", e.what());
      return 1;
    }
  }
  if (!exportMermaidGraphPath.empty()) {
    try {
      GraphSpec spec = loadGraphSpecFromJsonFile(exportMermaidGraphPath);
      const std::string mmd = generateMermaidForGraph(spec);
      std::fwrite(mmd.data(), 1, mmd.size(), stdout);
      return 0;
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Diagram export failed: %s\n", e.what());
      return 1;
    }
  }

  params.loop = params.bpm > 0.0f;
  if (params.gain < 0.0f) params.gain = 0.0f;
  if (params.gain > 1.5f) params.gain = 1.5f;
  if (params.click < 0.0f) params.click = 0.0f;
  if (params.click > 1.0f) params.click = 1.0f;

  // Utilities
  if (printTopo && !graphPath.empty()) {
    try {
      GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
      warnSidechainConnectivity(spec);
      printTopoOrderFromSpec(spec);
      printConnectionsSummary(spec);
      printPortsSummary(spec);
    } catch (...) {}
    // If only inspecting topology, exit early to avoid entering realtime
    if (wavPath.empty() && validatePath.empty() && listNodesPath.empty() && listParamsType.empty()) {
      return 0;
    }
  }
  if (!validatePath.empty()) return validateGraphJson(validatePath);
  if (!listNodesPath.empty()) return listNodesGraphJson(listNodesPath);
  if (listNodeTypes) {
    try {
      nlohmann::json schema;
      std::ifstream fs("docs/schema.graph.v1.json");
      if (fs.good()) {
        fs >> schema;
        const auto& enumArr = schema.at("properties").at("nodes").at("items").at("properties").at("type").at("enum");
        std::printf("Supported node types (%zu):\n", enumArr.size());
        for (const auto& e : enumArr) std::printf("- %s\n", e.get<std::string>().c_str());
      } else {
        std::printf("Supported node types: kick, clap, transport, delay, meter, compressor, reverb\n");
      }
    } catch (...) {
      std::printf("Supported node types: kick, clap, transport, delay, meter, compressor, reverb\n");
    }
    return 0;
  }
  if (!listParamsType.empty()) {
    if (listParamsType == std::string("kick")) {
      std::printf("kick params:\n");
      for (size_t idx = 0; idx < kKickParamMap.count; ++idx) {
        const auto& d = kKickParamMap.defs[idx];
        std::printf("%u %s [%g..%g] def=%g %s\n", d.id, d.name, d.minValue, d.maxValue, d.defaultValue, d.smoothing);
      }
      return 0;
    } else if (listParamsType == std::string("clap")) {
      std::printf("clap params:\n");
      for (size_t idx = 0; idx < kClapParamMap.count; ++idx) {
        const auto& d = kClapParamMap.defs[idx];
        std::printf("%u %s [%g..%g] def=%g %s\n", d.id, d.name, d.minValue, d.maxValue, d.defaultValue, d.smoothing);
      }
      return 0;
    } else {
      std::fprintf(stderr, "Unknown node type for --list-params: %s\n", listParamsType.c_str());
      return 1;
    }
  }

  std::signal(SIGINT, onSigInt);

  // EARLY realtime session handling (before graph/default paths)
  if (!sessionPath.empty() && wavPath.empty()) {
    // Resolve and validate session file
    std::fprintf(stderr, "[rt-session] resolved path=%s\n", sessionPath.c_str());
    {
      std::ifstream sf(sessionPath);
      if (!sf.good()) { std::fprintf(stderr, "Session file not found: %s\n", sessionPath.c_str()); return 1; }
    }
    try {
      SessionSpec sess = loadSessionSpecFromJsonFile(sessionPath);
      // Build per-rack graphs with prefixed node ids, synthesize base commands for one loop, compute loopLen
      std::vector<std::unique_ptr<Graph>> graphsOwned;
      struct RackRT { std::string rackId; std::vector<GraphSpec::CommandSpec> baseCmds; uint64_t loopLen=0; };
      std::vector<RackRT> rackRTs;
      const uint32_t sessionSrU32 = static_cast<uint32_t>((offlineSr > 0.0 ? offlineSr : 48000.0) + 0.5);
      std::fprintf(stderr, "[rt-session] starting: racks=%zu sr=%u\n", sess.racks.size(), sessionSrU32);
      // Determine solo/mute policy once
      bool anySoloGlobal = false; for (const auto& rr : sess.racks) if (rr.solo) { anySoloGlobal = true; break; }
      for (const auto& rr : sess.racks) {
        GraphSpec gs = loadGraphSpecFromJsonFile(rr.path);
        auto g = std::make_unique<Graph>();
        for (const auto& ns : gs.nodes) {
          auto node = createNodeFromSpec(ns);
          if (node) g->addNode(rr.id + ":" + ns.id, std::move(node));
        }
        if (gs.hasMixer) {
          std::vector<MixerChannel> chans;
          for (const auto& inp : gs.mixer.inputs) { MixerChannel mc; mc.id = rr.id + ":" + inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f); chans.push_back(mc);}        
          const float master = gs.mixer.masterPercent * (1.0f/100.0f);
          g->setMixer(std::make_unique<MixerNode>(std::move(chans), master, gs.mixer.softClip));
        }
        if (!gs.connections.empty()) {
          auto conns = gs.connections; for (auto& c : conns) { c.from = rr.id + ":" + c.from; c.to = rr.id + ":" + c.to; }
          g->setConnections(conns);
        }
        // Synthesize commands
        std::vector<GraphSpec::CommandSpec> cmds = gs.commands;
        if (gs.hasTransport) {
          GraphSpec::Transport tgen = gs.transport; uint32_t baseBars = (gs.transport.lengthBars > 0) ? gs.transport.lengthBars : 1u; if (rr.bars > 0) baseBars = rr.bars; tgen.lengthBars = baseBars;
          auto gen = generateCommandsFromTransport(tgen, sessionSrU32); cmds.insert(cmds.end(), gen.begin(), gen.end());
        }
        // Apply solo/mute policy: if any solo, only racks with rr.solo emit; otherwise skip muted
        const bool activeRack = anySoloGlobal ? rr.solo : !rr.muted;
        if (!activeRack) cmds.clear();
        // Resolve params and prefix nodeIds
        std::unordered_map<std::string, std::string> nodeIdToType; for (const auto& ns : gs.nodes) nodeIdToType.emplace(ns.id, ns.type);
        auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
          if (type == std::string("kick")) return resolveParamIdByName(kKickParamMap, name);
          if (type == std::string("clap")) return resolveParamIdByName(kClapParamMap, name);
          if (type == std::string("tb303_ext")) return resolveParamIdByName(kTb303ParamMap, name);
          if (type == std::string("mam_chip")) return resolveParamIdByName(kMamChipParamMap, name);
          return 0;
        };
        for (auto& c : cmds) { c.nodeId = rr.id + ":" + c.nodeId; if (c.paramId == 0 && !c.paramName.empty()) { auto it = nodeIdToType.find(c.nodeId.substr(rr.id.size()+1)); const std::string nodeType = (it != nodeIdToType.end()) ? it->second : std::string(); c.paramId = mapParam(nodeType, c.paramName);} }
        // Compute loopLen
        uint64_t loopLen = 0; if (gs.hasTransport) { const double bpm = (gs.transport.bpm > 0.0) ? gs.transport.bpm : 120.0; const double secPerBar = 4.0 * (60.0 / bpm); const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sessionSrU32 + 0.5); const uint32_t bars = (gs.transport.lengthBars > 0) ? gs.transport.lengthBars : 1u; loopLen = framesPerBar * static_cast<uint64_t>(bars);}        
        rackRTs.push_back(RackRT{rr.id, cmds, loopLen});
        graphsOwned.push_back(std::move(g));
        std::fprintf(stderr, "[rt-session] rack=%s cmds=%zu loopLen=%llu\n", rr.id.c_str(), cmds.size(), (unsigned long long)loopLen);
      }
      if (rackRTs.empty()) { std::fprintf(stderr, "[rt-session] error: no racks\n"); return 1; }
      size_t totalCmds = 0; for (const auto& r : rackRTs) totalCmds += r.baseCmds.size(); if (totalCmds == 0) std::fprintf(stderr, "[rt-session] error: synthesized zero commands across racks\n");
      // Start realtime renderer
      RealtimeSessionRenderer srt; SpscCommandQueue<16384> cmdQueue;
      srt.setCommandQueue(&cmdQueue); srt.setDiagnostics(printTriggers); srt.setMeters(printMeters || metersPerNode, metersIntervalSec);
      if (!metricsNdjsonPath.empty()) srt.setMetricsNdjson(metricsNdjsonPath.c_str(), metricsScopeRacks, metricsScopeBuses);
      // Configure session xfaders (if any)
      {
        std::vector<RealtimeSessionRenderer::Rack> cfg; cfg.reserve(graphsOwned.size());
        for (size_t i = 0; i < graphsOwned.size(); ++i) cfg.push_back(RealtimeSessionRenderer::Rack{graphsOwned[i].get(), sess.racks[i].id, sess.racks[i].gain, sess.racks[i].muted, sess.racks[i].solo});
        srt.setXfaders(sess.xfaders, cfg);
      }
      std::vector<RealtimeSessionRenderer::Rack> rracks; rracks.reserve(graphsOwned.size()); for (size_t i = 0; i < graphsOwned.size(); ++i) rracks.push_back(RealtimeSessionRenderer::Rack{graphsOwned[i].get(), sess.racks[i].id, sess.racks[i].gain, sess.racks[i].muted, sess.racks[i].solo});
      srt.start(rracks, sess.buses, sess.routes, offlineSr > 0.0 ? offlineSr : 48000.0, 2);
      // Enqueue session-level commands (absolute time or musical time, realtime)
      if (!sess.commands.empty()) {
        for (const auto& sc : sess.commands) {
          double resolvedTimeSec = sc.timeSec;

          // Resolve musical time if absolute time not provided
          if (sc.timeSec == 0.0 && !sc.rack.empty() && sc.bar > 0) {
            // Find the referenced rack
            auto rackIt = std::find_if(sess.racks.begin(), sess.racks.end(),
              [&](const SessionSpec::RackRef& r) { return r.id == sc.rack; });
            if (rackIt == sess.racks.end()) {
              std::fprintf(stderr, "[rt-session] warning: musical command references unknown rack '%s'\n", sc.rack.c_str());
              continue;
            }

            // Find the corresponding rack runtime info
            auto rackRtIt = std::find_if(rackRTs.begin(), rackRTs.end(),
              [&](const RackRT& r) { return r.rackId == sc.rack; });
            if (rackRtIt == rackRTs.end()) {
              std::fprintf(stderr, "[rt-session] warning: no runtime info for rack '%s'\n", sc.rack.c_str());
              continue;
            }

            // Convert musical time to seconds using the rack's transport
            const uint32_t bar = sc.bar - 1; // Convert to 0-based
            const uint32_t step = (sc.step > 0) ? sc.step - 1 : 0; // Convert to 0-based, default to bar start
            const uint32_t stepsPerBar = sc.res;

            if (rackRtIt->loopLen > 0) {
              const double secPerBar = static_cast<double>(rackRtIt->loopLen) / srt.sampleRate();
              const double stepsPerSec = static_cast<double>(stepsPerBar) / secPerBar;
              const double stepSec = static_cast<double>(step) / stepsPerSec;

              resolvedTimeSec = static_cast<double>(bar) * secPerBar + stepSec;
              std::fprintf(stderr, "[rt-session] resolved musical command: rack=%s bar=%u step=%u res=%u -> %.3f sec\n",
                sc.rack.c_str(), sc.bar, sc.step, sc.res, resolvedTimeSec);
            } else {
              std::fprintf(stderr, "[rt-session] warning: rack '%s' has zero loop length, cannot resolve musical time\n", sc.rack.c_str());
              continue;
            }
          } else if (sc.timeSec > 0.0 && (!sc.rack.empty() || sc.bar > 0)) {
            std::fprintf(stderr, "[rt-session] warning: command has both timeSec and musical fields, using timeSec\n");
          }

          Command cmd{};
          cmd.sampleTime = static_cast<uint64_t>(std::llround(resolvedTimeSec * srt.sampleRate()));
          cmd.nodeId = internNodeId(sc.nodeId);
          cmd.type = (sc.type == std::string("SetParam")) ? CommandType::SetParam : CommandType::SetParam;
          cmd.paramId = 0;
          cmd.value = sc.value;
          cmd.rampMs = sc.rampMs;
          while (gRunning.load() && !cmdQueue.push(cmd)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
      // Adjust pre-synthesized command timing to actual device sample rate if needed
      {
        const uint32_t srActual = static_cast<uint32_t>(srt.sampleRate() + 0.5);
        if (srActual != sessionSrU32) {
          const double scale = static_cast<double>(srActual) / static_cast<double>(sessionSrU32);
          for (auto& r : rackRTs) {
            for (auto& c : r.baseCmds) {
              c.sampleTime = static_cast<uint64_t>(std::llround(static_cast<double>(c.sampleTime) * scale));
            }
            r.loopLen = static_cast<uint64_t>(std::llround(static_cast<double>(r.loopLen) * scale));
          }
          std::fprintf(stderr, "[rt-session] rescaled commands to device sr=%u (scale=%.6f)\n", srActual, scale);
        }
      }
      // Optional: align transports (shift racks so the earliest first trigger begins at t=0)
      if (sess.alignTransports) {
        uint64_t earliest = UINT64_MAX;
        std::vector<uint64_t> firsts(rackRTs.size(), UINT64_MAX);
        for (size_t i = 0; i < rackRTs.size(); ++i) {
          for (const auto& c : rackRTs[i].baseCmds) {
            if (c.type == std::string("Trigger")) { firsts[i] = c.sampleTime; break; }
          }
          if (firsts[i] < earliest) earliest = firsts[i];
        }
        if (earliest != UINT64_MAX) {
          for (size_t i = 0; i < rackRTs.size(); ++i) {
            if (firsts[i] == UINT64_MAX) continue;
            const int64_t shift = static_cast<int64_t>(firsts[i]) - static_cast<int64_t>(earliest);
            if (shift > 0) {
              const uint64_t u = static_cast<uint64_t>(shift);
              for (auto& c : rackRTs[i].baseCmds) c.sampleTime -= std::min(c.sampleTime, u);
            }
          }
        }
      }

      // Initial enqueue (globally time-sorted merge across racks)
      {
        // Determine active racks for enqueue (skip muted; if any solo, only solo)
        bool anySolo = false; for (const auto& rr : sess.racks) if (rr.solo) { anySolo = true; break; }
        std::vector<bool> activeFlags; activeFlags.reserve(sess.racks.size());
        for (const auto& rr : sess.racks) activeFlags.push_back(anySolo ? rr.solo : !rr.muted);
        struct Item { uint64_t t; size_t ri; size_t ci; };
        auto cmp = [](const Item& a, const Item& b){ return a.t > b.t; };
        std::priority_queue<Item, std::vector<Item>, decltype(cmp)> pq(cmp);
        for (size_t i = 0; i < rackRTs.size(); ++i) if (i < activeFlags.size() && activeFlags[i]) { if (!rackRTs[i].baseCmds.empty()) pq.push(Item{rackRTs[i].baseCmds[0].sampleTime, i, 0}); }
        size_t totalPushed = 0;
        while (!pq.empty() && gRunning.load()) {
          Item it = pq.top(); pq.pop(); const auto& c = rackRTs[it.ri].baseCmds[it.ci];
          Command cmd{}; cmd.sampleTime = c.sampleTime; cmd.nodeId = internNodeId(c.nodeId);
          cmd.type = (c.type == std::string("Trigger")) ? CommandType::Trigger : (c.type == std::string("SetParam")) ? CommandType::SetParam : CommandType::SetParamRamp;
          cmd.paramId = c.paramId; cmd.value = c.value; cmd.rampMs = c.rampMs;
          while (gRunning.load() && !cmdQueue.push(cmd)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
          if (!gRunning.load()) break; totalPushed++;
          const size_t nextCi = it.ci + 1; if (nextCi < rackRTs[it.ri].baseCmds.size()) pq.push(Item{rackRTs[it.ri].baseCmds[nextCi].sampleTime, it.ri, nextCi});
        }
        std::fprintf(stderr, "[rt-session] init enqueued total cmds=%zu across %zu racks (time-sorted)\n", totalPushed, rackRTs.size());
      }
      // Feeder thread
      std::thread feeder([&cmdQueue, &rackRTs, &srt, sess]() mutable {
        const uint64_t desiredAhead = static_cast<uint64_t>(3.0 * srt.sampleRate());
        std::vector<uint64_t> nextOffset(rackRTs.size(), 0ull); for (size_t i = 0; i < rackRTs.size(); ++i) nextOffset[i] = rackRTs[i].loopLen;
        bool anySolo = false; for (const auto& rr : sess.racks) if (rr.solo) { anySolo = true; break; }
        std::vector<bool> activeFlags; activeFlags.reserve(sess.racks.size());
        for (const auto& rr : sess.racks) activeFlags.push_back(anySolo ? rr.solo : !rr.muted);
        while (gRunning.load()) {
          const uint64_t now = static_cast<uint64_t>(srt.sampleCounter());
          // collect eligible racks
          std::vector<size_t> elig; elig.reserve(rackRTs.size());
          for (size_t i = 0; i < rackRTs.size(); ++i) if (i < activeFlags.size() && activeFlags[i]) { if (rackRTs[i].loopLen > 0 && nextOffset[i] <= now + desiredAhead) elig.push_back(i); }
          if (!elig.empty()) {
            struct Item { uint64_t t; size_t ri; size_t ci; };
            auto cmp = [](const Item& a, const Item& b){ return a.t > b.t; };
            std::priority_queue<Item, std::vector<Item>, decltype(cmp)> pq(cmp);
            for (size_t idx : elig) {
              if (!rackRTs[idx].baseCmds.empty()) pq.push(Item{rackRTs[idx].baseCmds[0].sampleTime + nextOffset[idx], idx, 0});
            }
            while (!pq.empty() && gRunning.load()) {
              Item it = pq.top(); pq.pop(); const auto& c = rackRTs[it.ri].baseCmds[it.ci];
              Command cmd{}; cmd.sampleTime = c.sampleTime + nextOffset[it.ri]; cmd.nodeId = internNodeId(c.nodeId); cmd.type = (c.type == std::string("Trigger")) ? CommandType::Trigger : (c.type == std::string("SetParam")) ? CommandType::SetParam : CommandType::SetParamRamp; cmd.paramId = c.paramId; cmd.value = c.value; cmd.rampMs = c.rampMs;
              while (gRunning.load() && !cmdQueue.push(cmd)) std::this_thread::sleep_for(std::chrono::milliseconds(1)); if (!gRunning.load()) break;
              const size_t nextCi = it.ci + 1; if (nextCi < rackRTs[it.ri].baseCmds.size()) pq.push(Item{rackRTs[it.ri].baseCmds[nextCi].sampleTime + nextOffset[it.ri], it.ri, nextCi});
            }
            for (size_t idx : elig) nextOffset[idx] += rackRTs[idx].loopLen;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      });
      // Wait loop with optional quit-after
      const double sr = srt.sampleRate();
      while (gRunning.load()) {
        if (quitAfterSec > 0.0) { const double t = static_cast<double>(srt.sampleCounter()) / sr; if (t >= quitAfterSec) { gRunning.store(false); break; } }
        if (isStdinReady()) { char buf[4]; (void)read(STDIN_FILENO, buf, sizeof(buf)); gRunning.store(false); break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (feeder.joinable()) feeder.join(); srt.stop();
      return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "Realtime session failed: %s\n", e.what()); return 1; }
  }

  // Offline render path to WAV if requested
  if (!wavPath.empty()) {
    const uint32_t channels = 2;
    const uint32_t sr = static_cast<uint32_t>(offlineSr + 0.5);
    uint64_t totalFrames = 0;

    // Session offline path
    if (!sessionPath.empty()) {
      try {
        SessionSpec sess = loadSessionSpecFromJsonFile(sessionPath);
        if (offlineSr > 0.0) sess.sampleRate = sr;
        SessionRuntime runtime; runtime.loadFromSpec(sess);

        // Handle loop-aware duration planning
        uint32_t maxLoops = 1;
        if (sess.loop && sess.durationSec > 0.0) {
          // For looping sessions, calculate frames for multiple loops
          uint64_t singleLoopFrames = runtime.planTotalFrames(tailMs, false, 1);
          if (singleLoopFrames > 0) {
            double singleLoopSec = static_cast<double>(singleLoopFrames) / sr;
            maxLoops = static_cast<uint32_t>(std::ceil(sess.durationSec / singleLoopSec));
            totalFrames = runtime.planTotalFrames(tailMs, true, maxLoops);
            std::fprintf(stderr, "[offline-session] looping session: %u loops (%.3fs each), total duration=%.3f sec\n",
              maxLoops, singleLoopSec, static_cast<double>(totalFrames) / sr);
          } else {
            totalFrames = runtime.planTotalFrames(tailMs);
          }
        } else {
          totalFrames = runtime.planTotalFrames(tailMs);
        }

        if (overrideDurationSec >= 0.0) totalFrames = static_cast<uint64_t>(overrideDurationSec * static_cast<double>(sr) + 0.5);
        // Enable per-rack meters if requested
        std::vector<SessionRuntime::RackStats> rstats;
        runtime.setPerRackMeters(printMeters);
        runtime.setPerRackCpu(cpuStats || cpuStatsPerNode);

        std::vector<float> interleaved;
        if (sess.loop && sess.durationSec > 0.0 && maxLoops > 1) {
          // Use loop-aware rendering for looping sessions
          std::fprintf(stderr, "[offline-session] rendering %u loops for session\n", maxLoops);
          interleaved = runtime.renderOfflineWithLoop(totalFrames, maxLoops, printMeters ? &rstats : nullptr);
        } else {
          // Standard rendering for non-looping sessions
          interleaved = runtime.renderOffline(totalFrames, printMeters ? &rstats : nullptr);
        }
        AudioFileSpec spec; spec.format = outFormat; spec.bitDepth = pcm16 ? BitDepth::Pcm16 : outDepth; spec.sampleRate = sr; spec.channels = channels;
        writeWithExtAudioFile(wavPath, spec, interleaved);
        const double seconds = static_cast<double>(totalFrames) / static_cast<double>(sr);
        std::fprintf(stderr, "Exported session to %s (frames=%llu, %.3fs)\n", wavPath.c_str(), (unsigned long long)interleaved.size()/channels, seconds);
        if (printMeters && !rstats.empty()) {
          for (const auto& st : rstats) {
            std::fprintf(stderr, "  Rack %s: peak=%.2f dBFS rms=%.2f dBFS\n", st.id.c_str(), st.peakDb, st.rmsDb);
          }
        }
        return 0;
      } catch (const std::exception& e) {
        std::fprintf(stderr, "Session render failed: %s\n", e.what());
        return 1;
      }
    }

    Graph graph;
    if (!graphPath.empty()) {
      try {
        if (schemaStrict) {
          std::string diag;
          const std::string schemaPath = std::string("docs/schema.graph.v1.json");
          int vs = validateJsonWithDraft2020(graphPath, schemaPath, diag);
          if (vs != 0) { std::fprintf(stderr, "Schema validation failed: %s\n", diag.c_str()); return 1; }
        }
        GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
        warnSidechainConnectivity(spec);
        if (randomSeedOverride != 0) setGlobalSeed(randomSeedOverride);
        else if (spec.randomSeed != 0) setGlobalSeed(spec.randomSeed);
        for (const auto& ns : spec.nodes) {
          auto node = createNodeFromSpec(ns);
          if (node) graph.addNode(ns.id, std::move(node));
        }
        if (spec.hasMixer) {
          std::vector<MixerChannel> chans;
          for (const auto& inp : spec.mixer.inputs) {
            MixerChannel mc; mc.id = inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f);
            chans.push_back(mc);
          }
          const float master = spec.mixer.masterPercent * (1.0f/100.0f);
          graph.setMixer(std::make_unique<MixerNode>(std::move(chans), master, spec.mixer.softClip));
        }
      if (!spec.connections.empty()) {
          graph.setConnections(spec.connections);
        }
      if (printTopo) printTopoOrderFromSpec(spec);
      if (metersPerNode) graph.enableStats(true);
      if (cpuStats || cpuStatsPerNode) graph.enableCpuStats(true);
      if (cpuStats || cpuStatsPerNode) graph.enableCpuStats(true);
      } catch (const std::exception& e) {
        std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
        return 1;
      }
    } else {
      KickParams p = params;
      p.loop = false;
      graph.addNode("kick_default", std::make_unique<KickNode>(p));
    }
    std::vector<float> interleaved;
    if (!graphPath.empty()) {
      try {
        GraphSpec spec2 = loadGraphSpecFromJsonFile(graphPath);
        if (randomSeedOverride != 0) setGlobalSeed(randomSeedOverride);
        else if (spec2.randomSeed != 0) setGlobalSeed(spec2.randomSeed);
        // Synthesize commands from transport, if present
        std::vector<GraphSpec::CommandSpec> cmds = spec2.commands;
        if (spec2.hasTransport) {
          // Generate transport commands covering requested bars/loops
          GraphSpec::Transport tgen = spec2.transport;
          const uint32_t baseBars = spec2.transport.lengthBars ? spec2.transport.lengthBars : 1u;
          uint32_t useBars = overrideBars > 0 ? overrideBars : baseBars;
          uint32_t loops = overrideLoopCount > 0 ? overrideLoopCount : 1u;
          if ((loopMinutes > 0.0 || loopSeconds > 0.0) && useBars > 0) {
            const double targetSec = (loopMinutes > 0.0 ? loopMinutes * 60.0 : loopSeconds);
            if (targetSec > 0.0) {
              // compute seconds per bar with ramps by averaging first bar
              const double bpm = spec2.transport.bpm > 0.0 ? spec2.transport.bpm : 120.0;
              const double secPerBar = 4.0 * (60.0 / bpm);
              const uint32_t perLoopBars = useBars;
              const double perLoopSec = secPerBar * static_cast<double>(perLoopBars);
              loops = static_cast<uint32_t>(std::ceil(targetSec / std::max(0.001, perLoopSec)));
              if (loops == 0) loops = 1;
            }
          }
          tgen.lengthBars = useBars * loops;
          auto gen = generateCommandsFromTransport(tgen, sr);
          cmds.insert(cmds.end(), gen.begin(), gen.end());
        }
        // Resolve named params to IDs based on node type
        {
          std::unordered_map<std::string, std::string> nodeIdToType;
          for (const auto& ns : spec2.nodes) nodeIdToType.emplace(ns.id, ns.type);
          auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
            if (type == std::string("kick")) return resolveParamIdByName(kKickParamMap, name);
            if (type == std::string("clap")) return resolveParamIdByName(kClapParamMap, name);
            if (type == std::string("tb303_ext")) return resolveParamIdByName(kTb303ParamMap, name);
            return 0;
          };
          for (auto& c : cmds) {
            if (c.paramId == 0 && !c.paramName.empty()) {
              auto it = nodeIdToType.find(c.nodeId);
              const std::string nodeType = (it != nodeIdToType.end()) ? it->second : std::string();
              c.paramId = mapParam(nodeType, c.paramName);
            }
          }
        }
        // Optional event dump (offline): print synthesized commands with timing
        if (dumpEvents) {
          const double bpm = spec2.hasTransport ? spec2.transport.bpm : 120.0;
          const uint32_t res = spec2.hasTransport ? spec2.transport.resolution : 16u;
          dumpCommands(cmds, sr, bpm, res, "CMD");
        }
        // Determine totalFrames (auto unless overridden)
        if (overrideDurationSec >= 0.0) {
          totalFrames = static_cast<uint64_t>(overrideDurationSec * static_cast<double>(sr) + 0.5);
        } else if (spec2.hasTransport) {
          auto bpmAtBar = [&](uint32_t barIndex) -> double {
            double bpm = spec2.transport.bpm;
            for (const auto& p : spec2.transport.tempoRamps) { if (p.bar <= barIndex) bpm = p.bpm; }
            return bpm;
          };
          auto framesPerBarAt = [&](uint32_t barIndex) -> uint64_t {
            const double secPerBeat = 60.0 / bpmAtBar(barIndex);
            const double secPerBar = 4.0 * secPerBeat;
            return static_cast<uint64_t>(secPerBar * static_cast<double>(sr) + 0.5);
          };
          const uint32_t baseBars = spec2.transport.lengthBars ? spec2.transport.lengthBars : 1u;
          const uint32_t useBars = overrideBars > 0 ? overrideBars : baseBars;
          const uint32_t loops = overrideLoopCount > 0 ? overrideLoopCount : 1u;
          const uint64_t totalBars = static_cast<uint64_t>(useBars) * static_cast<uint64_t>(loops);
          totalFrames = 0;
          for (uint64_t b = 0; b < totalBars; ++b) totalFrames += framesPerBarAt(static_cast<uint32_t>(b));
        } else if (!cmds.empty()) {
          uint64_t last = 0; for (const auto& c : cmds) if (c.sampleTime > last) last = c.sampleTime;
          totalFrames = last;
        } else {
          totalFrames = static_cast<uint64_t>(2.0 * static_cast<double>(sr) + 0.5);
        }
        // Add preroll (graph latency) and tail
        // Suggest longer tail for long delays/reverb if not overridden
        double tailMsLocal = tailMs;
        if (!tailOverridden) {
          double maxDelayMs = 0.0; bool hasReverb = false;
          for (const auto& ns : spec2.nodes) {
            if (ns.type == std::string("delay")) {
              try { nlohmann::json pj = nlohmann::json::parse(ns.paramsJson); maxDelayMs = std::max(maxDelayMs, pj.value("delayMs", 0.0)); } catch (...) {}
            } else if (ns.type == std::string("reverb")) {
              hasReverb = true;
            }
          }
          double suggested = 250.0;
          if (maxDelayMs > 0.0) suggested = std::max(suggested, std::min(6000.0, maxDelayMs * 2.0));
          if (hasReverb) suggested = std::max(suggested, 1000.0);
          tailMsLocal = suggested;
        }
        const uint64_t preroll = computeGraphPrerollSamples(spec2, sr);
        totalFrames += preroll + static_cast<uint64_t>((tailMsLocal / 1000.0) * static_cast<double>(sr) + 0.5);
        // Print planned duration info when looping or bars override is used
        if (overrideBars > 0 || overrideLoopCount > 0 || loopMinutes > 0.0 || loopSeconds > 0.0) {
          const double plannedSec = static_cast<double>(totalFrames) / static_cast<double>(sr);
          std::fprintf(stderr, "Planned duration: %s (%.3fs) including preroll %.3fs and tail %.3fs\n",
                       formatDuration(plannedSec).c_str(), plannedSec,
                       static_cast<double>(preroll) / static_cast<double>(sr), tailMsLocal / 1000.0);
        }
        interleaved = renderGraphWithCommands(graph, cmds, sr, channels, totalFrames);
      } catch (...) {
        if (totalFrames == 0) totalFrames = static_cast<uint64_t>(2.0 * static_cast<double>(sr) + 0.5);
        interleaved = (offlineThreads > 1) ? renderGraphInterleavedParallel(graph, sr, channels, totalFrames, offlineThreads)
                                           : renderGraphInterleaved(graph, sr, channels, totalFrames);
      }
    } else {
      // No graph file: use duration override or default + tail
      if (overrideDurationSec >= 0.0) totalFrames = static_cast<uint64_t>(overrideDurationSec * static_cast<double>(sr) + 0.5);
      else totalFrames = static_cast<uint64_t>(2.0 * static_cast<double>(sr) + 0.5);
      totalFrames += static_cast<uint64_t>((tailMs / 1000.0) * static_cast<double>(sr) + 0.5);
      interleaved = (offlineThreads > 1) ? renderGraphInterleavedParallel(graph, sr, channels, totalFrames, offlineThreads)
                                         : renderGraphInterleaved(graph, sr, channels, totalFrames);
    }

    AudioFileSpec spec;
    spec.format = outFormat;
    spec.bitDepth = pcm16 ? BitDepth::Pcm16 : outDepth;
    spec.sampleRate = sr;
    spec.channels = channels;

    try {
      // Optional normalization
      double prePeakDb = 0.0, preRmsDb = 0.0;
      computePeakAndRms(interleaved, channels, prePeakDb, preRmsDb);
      double appliedGainDb = 0.0;
      if (doNormalize && std::isfinite(prePeakDb)) {
        appliedGainDb = peakTargetDb - prePeakDb;
        const double g = std::pow(10.0, appliedGainDb / 20.0);
        for (auto& s : interleaved) s = static_cast<float>(static_cast<double>(s) * g);
      }
      writeWithExtAudioFile(wavPath, spec, interleaved);
      double peakDb = 0.0, rmsDb = 0.0;
      computePeakAndRms(interleaved, channels, peakDb, rmsDb);
      const double seconds = static_cast<double>(totalFrames) / static_cast<double>(sr);
      const std::string hhmmss = formatDuration(seconds);
      const double nyquist = static_cast<double>(sr) * 0.5;
      std::fprintf(stderr,
                   "Exported %s\n  Frames: %llu\n  Duration: %s (%.3fs)\n  Sample rate: %u Hz (Nyquist %.1f Hz)\n  Channels: %u\n  Format: %s / %s\n  Peak: %.2f dBFS (pre: %.2f dBFS, gain: %+0.2f dB)\n  RMS: %.2f dBFS\n",
                   wavPath.c_str(), static_cast<unsigned long long>(totalFrames), hhmmss.c_str(), seconds,
                   sr, nyquist, channels, toStr(spec.format), toStr(spec.bitDepth), peakDb, prePeakDb, appliedGainDb, rmsDb);
      if (printMeters) {
        // duplicate a concise meters line for easy parsing
        std::fprintf(stderr, "Meters: peak_dBFS=%.2f rms_dBFS=%.2f\n", peakDb, rmsDb);
      }
      if (metersPerNode) {
        const auto nodeMeters = graph.getNodeMeters(channels);
        for (const auto& m : nodeMeters) {
          const bool inactive = (!std::isfinite(m.peakDb) && !std::isfinite(m.rmsDb));
          if (inactive) {
            std::fprintf(stderr, "Node %s: inactive\n", m.id.c_str());
          } else {
            std::fprintf(stderr, "Node %s: peak=%.2f dBFS rms=%.2f dBFS\n", m.id.c_str(), m.peakDb, m.rmsDb);
          }
        }
      }
      if (cpuStats || cpuStatsPerNode) {
        const auto s = graph.getCpuSummary();
        std::fprintf(stderr, "CPU block avg=%.3fms max=%.3fms (avg=%.1f%% max=%.1f%%) xruns=%llu blocks=%llu\n",
                     s.avgMs, s.maxMs, s.avgPercent, s.maxPercent,
                     static_cast<unsigned long long>(s.overruns), static_cast<unsigned long long>(s.blocks));
        if (cpuStatsPerNode) {
          const auto per = graph.getPerNodeCpu();
          for (const auto& n : per) std::fprintf(stderr, "  %s: avg=%.1fus max=%.1fus\n", n.id.c_str(), n.avgUs, n.maxUs);
        }
      }
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Audio file write failed: %s\n", e.what());
      return 1;
    }
    return 0;
  }

  // Realtime renderer path via session OR graph
  Graph graph;
  std::thread transportFeeder;
  uint64_t rtLoopLen = 0; // frames
  if (!sessionPath.empty()) {
    // Defer: the session realtime branch is handled below in this control flow.
  } else if (!graphPath.empty()) {
    std::fprintf(stderr, "[rt-graph] starting: path=%s\n", graphPath.c_str());
    try {
      if (schemaStrict) {
        std::string diag;
        const std::string schemaPath = std::string("docs/schema.graph.v1.json");
        int vs = validateJsonWithDraft2020(graphPath, schemaPath, diag);
        if (vs != 0) { std::fprintf(stderr, "Schema validation failed: %s\n", diag.c_str()); return 1; }
      }
      GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
      if (randomSeedOverride != 0) setGlobalSeed(randomSeedOverride);
      else if (spec.randomSeed != 0) setGlobalSeed(spec.randomSeed);
      for (const auto& ns : spec.nodes) {
        auto node = createNodeFromSpec(ns);
        if (node) graph.addNode(ns.id, std::move(node));
      }
      if (spec.hasMixer) {
        std::vector<MixerChannel> chans;
        for (const auto& inp : spec.mixer.inputs) {
          MixerChannel mc; mc.id = inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f);
          chans.push_back(mc);
        }
        const float master = spec.mixer.masterPercent * (1.0f/100.0f);
        graph.setMixer(std::make_unique<MixerNode>(std::move(chans), master, spec.mixer.softClip));
      }
      if (!spec.connections.empty()) {
        graph.setConnections(spec.connections);
      }
      // Provide port descriptors to graph (for future adapters)
      graph.setPortDescriptors(spec.nodes);
      if (metersPerNode) graph.enableStats(true);
      if (cpuStats || cpuStatsPerNode) graph.enableCpuStats(true);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Failed to load graph JSON: %s\n", e.what());
      return 1;
    }
  } else {
    graph.addNode("kick_default", std::make_unique<KickNode>(params));
  }
  RealtimeGraphRenderer rt;
  SpscCommandQueue<2048> cmdQueue;
    rt.setCommandQueue(&cmdQueue);
    rt.setCmdQueueDebug(rtDebugFeed);
  // Try to infer transport resolution and bpm from graph for diagnostics; defaults: res=16, bpm=120
  uint32_t diagRes = 16;
  double diagBpm = 120.0;
  if (!graphPath.empty()) {
    try {
      GraphSpec tmp = loadGraphSpecFromJsonFile(graphPath);
      if (tmp.hasTransport) {
        if (tmp.transport.resolution > 0) diagRes = tmp.transport.resolution;
        if (tmp.transport.bpm > 0.0f) diagBpm = tmp.transport.bpm;
      }
    } catch (...) {}
  }
  rt.setDiagnostics(printTriggers, diagBpm, diagRes);
  rt.setDiagLoop(rtLoopLen);
  rt.setTransportEmitEnabled(false); // all triggers come from pre-enqueued commands for parity
  try {
    rt.start(graph, 48000.0, 2);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Realtime start failed: %s\n", e.what());
    return 1;
  }

  // Always allow Ctrl-C or Enter to stop in realtime
  // If a graph was provided and it has commands/transport, enqueue them (demo horizon)
  if (!graphPath.empty()) {
    try {
      GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
      // Build initial command set (explicit + transport)
      std::vector<GraphSpec::CommandSpec> baseCmds = spec.commands;
      if (spec.hasTransport) {
        // Generate a multi-loop chunk in one shot to avoid boundary duplication/rounding artifacts
        GraphSpec::Transport chunk = spec.transport;
        const uint32_t baseBarsRT = (spec.transport.lengthBars > 0) ? spec.transport.lengthBars : 1u;
        const uint32_t chunkLoops = 1u; // single-loop; we'll replicate explicitly
        chunk.lengthBars = baseBarsRT * chunkLoops;
        auto gen = generateCommandsFromTransport(chunk, static_cast<uint32_t>(rt.sampleRate() + 0.5));
        baseCmds.insert(baseCmds.end(), gen.begin(), gen.end());
      }
      // Resolve named params to IDs based on node type
      {
        std::unordered_map<std::string, std::string> nodeIdToType;
        for (const auto& ns : spec.nodes) nodeIdToType.emplace(ns.id, ns.type);
        auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
          if (type == std::string("kick")) return resolveParamIdByName(kKickParamMap, name);
          if (type == std::string("clap")) return resolveParamIdByName(kClapParamMap, name);
          if (type == std::string("tb303_ext")) return resolveParamIdByName(kTb303ParamMap, name);
          if (type == std::string("mam_chip")) return resolveParamIdByName(kMamChipParamMap, name);
          return 0;
        };
        for (auto& c : baseCmds) {
          if (c.paramId == 0 && !c.paramName.empty()) {
            auto it = nodeIdToType.find(c.nodeId);
            const std::string nodeType = (it != nodeIdToType.end()) ? it->second : std::string();
            c.paramId = mapParam(nodeType, c.paramName);
          }
        }
      }
      // Sort by time to preserve draining order in the SPSC queue
      std::sort(baseCmds.begin(), baseCmds.end(), [](const auto& a, const auto& b){ return a.sampleTime < b.sampleTime; });
      // Repeat transport loop up to horizon (quit-after or ~60s)
      // Horizon is exact multiple of loopLen to avoid a boundary block without triggers
      uint64_t loopsAhead = 16;
      if (quitAfterSec > 0.0 && rtLoopLen > 0) {
        loopsAhead = static_cast<uint64_t>(
          std::ceil((quitAfterSec * rt.sampleRate()) / static_cast<double>(rtLoopLen))
        );
      }
      uint64_t horizonFrames = (rtLoopLen > 0)
        ? (loopsAhead * rtLoopLen)
        : static_cast<uint64_t>(60.0 * rt.sampleRate());
      // Determine exact loop length in frames from transport (bars × framesPerBar).
      // Using exact bar length avoids gaps/overlaps at loop boundaries, ensuring continuous groove.
      uint64_t loopLen = 0;
      if (spec.hasTransport) {
        const double bpm = (spec.transport.bpm > 0.0) ? spec.transport.bpm : 120.0;
        const double secPerBeat = 60.0 / bpm;
        const double secPerBar = 4.0 * secPerBeat;
        const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * rt.sampleRate() + 0.5);
        const uint32_t bars = (spec.transport.lengthBars > 0) ? spec.transport.lengthBars : 1u;
        loopLen = framesPerBar * static_cast<uint64_t>(bars);
      }
      if (loopLen == 0) {
        // Fallback: derive from last event if transport is absent
        for (const auto& c : baseCmds) if (c.sampleTime > loopLen) loopLen = c.sampleTime;
      }
      rtLoopLen = loopLen;
      // Provide precise loop length to realtime diagnostics so Loop N prints at exact boundaries
      rt.setDiagLoop(rtLoopLen);
      std::vector<GraphSpec::CommandSpec> realtimeCmds;
      const bool startFeeder = (spec.hasTransport && loopLen > 0);
      // Use baseCmds as-is if they already span the initial horizon; otherwise replicate to cover it
      uint64_t baseSpan = 0; for (const auto& c : baseCmds) if (c.sampleTime > baseSpan) baseSpan = c.sampleTime;
      // For realtime with feeder, enqueue exactly one full loop now; feeder extends horizon later
      if (startFeeder) {
        realtimeCmds = baseCmds; // single loop
  } else if (!sessionPath.empty()) {
    // Early file existence check for better UX
    {
      std::ifstream sf(sessionPath);
      if (!sf.good()) { std::fprintf(stderr, "Session file not found: %s\n", sessionPath.c_str()); return 1; }
    }
    std::fprintf(stderr, "[rt-session] starting: path=%s\n", sessionPath.c_str());
    // Realtime session: build graphs per rack and unified command feeder
    try {
      SessionSpec sess = loadSessionSpecFromJsonFile(sessionPath);
      std::vector<std::unique_ptr<Graph>> graphsOwned;
      std::vector<Graph*> graphsPtrs;
      struct RackRT { std::string rackId; std::vector<GraphSpec::CommandSpec> baseCmds; uint64_t loopLen=0; };
      std::vector<RackRT> rackRTs;

      const uint32_t sessionSrU32 = static_cast<uint32_t>((offlineSr > 0.0 ? offlineSr : 48000.0) + 0.5);
      std::fprintf(stderr, "[rt-session] racks=%zu sr=%u\n", sess.racks.size(), sessionSrU32);
      // Determine solo/mute policy once
      bool anySoloGlobal2 = false; for (const auto& rr : sess.racks) if (rr.solo) { anySoloGlobal2 = true; break; }
      // Build graphs with rack-prefixed node ids; build base commands per rack
      for (const auto& rr : sess.racks) {
        GraphSpec gs = loadGraphSpecFromJsonFile(rr.path);
        auto g = std::make_unique<Graph>();
        // Add nodes with prefixed ids
        for (const auto& ns : gs.nodes) {
          auto node = createNodeFromSpec(ns);
          if (node) {
            const std::string nid = rr.id + ":" + ns.id;
            g->addNode(nid, std::move(node));
          }
        }
        // Mixer with prefixed channels
        if (gs.hasMixer) {
          std::vector<MixerChannel> chans;
          for (const auto& inp : gs.mixer.inputs) {
            MixerChannel mc; mc.id = rr.id + ":" + inp.id; mc.gain = inp.gainPercent * (1.0f/100.0f);
            chans.push_back(mc);
          }
          const float master = gs.mixer.masterPercent * (1.0f/100.0f);
          g->setMixer(std::make_unique<MixerNode>(std::move(chans), master, gs.mixer.softClip));
        }
        // Connections with prefix
        if (!gs.connections.empty()) {
          std::vector<GraphSpec::Connection> conns = gs.connections;
          for (auto& c : conns) { c.from = rr.id + ":" + c.from; c.to = rr.id + ":" + c.to; }
          g->setConnections(conns);
        }
        graphsPtrs.push_back(g.get());
        graphsOwned.push_back(std::move(g));

        // Build base commands (transport + explicit) with prefixed nodeIds and overrides
        std::vector<GraphSpec::CommandSpec> cmds = gs.commands;
        if (gs.hasTransport) {
          // For realtime, generate exactly one transport loop chunk. Replication is handled by the feeder.
          GraphSpec::Transport tgen = gs.transport;
          uint32_t baseBars = (gs.transport.lengthBars > 0) ? gs.transport.lengthBars : 1u;
          if (rr.bars > 0) baseBars = rr.bars;
          tgen.lengthBars = baseBars;
          auto gen = generateCommandsFromTransport(tgen, sessionSrU32);
          cmds.insert(cmds.end(), gen.begin(), gen.end());
        }
        // Apply solo/mute policy
        const bool activeRack2 = anySoloGlobal2 ? rr.solo : !rr.muted;
        if (!activeRack2) cmds.clear();
        // Resolve named params and prefix nodeIds
        std::unordered_map<std::string, std::string> nodeIdToType;
        for (const auto& ns : gs.nodes) nodeIdToType.emplace(ns.id, ns.type);
        auto mapParam = [](const std::string& type, const std::string& name) -> uint16_t {
          if (type == std::string("kick")) return resolveParamIdByName(kKickParamMap, name);
          if (type == std::string("clap")) return resolveParamIdByName(kClapParamMap, name);
          if (type == std::string("tb303_ext")) return resolveParamIdByName(kTb303ParamMap, name);
          if (type == std::string("mam_chip")) return resolveParamIdByName(kMamChipParamMap, name);
          return 0;
        };
        for (auto& c : cmds) {
          c.nodeId = rr.id + ":" + c.nodeId;
          if (c.paramId == 0 && !c.paramName.empty()) {
            auto it = nodeIdToType.find(c.nodeId.substr(rr.id.size()+1));
            const std::string nodeType = (it != nodeIdToType.end()) ? it->second : std::string();
            c.paramId = mapParam(nodeType, c.paramName);
          }
        }
        // Apply per-rack start offset in frames (positive delays start; negative advances and clips pre-zero events)
        if (rr.startOffsetFrames != 0) {
          const int64_t off = rr.startOffsetFrames;
          if (off > 0) {
            const uint64_t uoff = static_cast<uint64_t>(off);
            for (auto& c : cmds) c.sampleTime += uoff;
          } else {
            const uint64_t adv = static_cast<uint64_t>(-off);
            // drop commands that would occur before t=0
            cmds.erase(std::remove_if(cmds.begin(), cmds.end(), [&](const auto& c){ return c.sampleTime <= adv; }), cmds.end());
            for (auto& c : cmds) c.sampleTime -= adv;
          }
        }
        // Compute loopLen from transport
        uint64_t loopLen2 = 0;
        if (gs.hasTransport) {
          const double bpm = (gs.transport.bpm > 0.0) ? gs.transport.bpm : 120.0;
          const double secPerBar = 4.0 * (60.0 / bpm);
          const uint64_t framesPerBar = static_cast<uint64_t>(secPerBar * sessionSrU32 + 0.5);
          const uint32_t bars = (gs.transport.lengthBars > 0) ? gs.transport.lengthBars : 1u;
          loopLen2 = framesPerBar * static_cast<uint64_t>(bars);
        }
        rackRTs.push_back(RackRT{rr.id, cmds, loopLen2});
        std::fprintf(stderr, "[rt-session] rack=%s cmds=%zu loopLen=%llu\n", rr.id.c_str(), cmds.size(), (unsigned long long)loopLen2);
        for (size_t i = 0; i < std::min<size_t>(cmds.size(), 6); ++i) {
          const auto& c = cmds[i];
          std::fprintf(stderr, "  cmd[%zu] t=%llu type=%s node=%s pid=%u val=%.3f\n", i, (unsigned long long)c.sampleTime,
                       c.type.c_str(), c.nodeId.c_str(), c.paramId, (double)c.value);
        }
      }
      if (rackRTs.empty()) { std::fprintf(stderr, "[rt-session] error: no racks\n"); return 1; }
      size_t totalCmds = 0; for (const auto& r : rackRTs) totalCmds += r.baseCmds.size();
      if (totalCmds == 0) { std::fprintf(stderr, "[rt-session] error: synthesized zero commands across racks\n"); }

      // Start realtime session renderer
      RealtimeSessionRenderer srt;
      srt.setCommandQueue(&cmdQueue);
      srt.setDiagnostics(printTriggers);
      // Enable periodic per-rack meters when --meters is provided (or keep per-node flag compatibility)
      srt.setMeters(printMeters || metersPerNode, metersIntervalSec);
      if (!metricsNdjsonPath.empty()) srt.setMetricsNdjson(metricsNdjsonPath.c_str(), metricsScopeRacks, metricsScopeBuses);
      // Configure session xfaders (if any)
      {
        std::vector<RealtimeSessionRenderer::Rack> cfg; cfg.reserve(graphsOwned.size());
        for (size_t i = 0; i < graphsOwned.size(); ++i) cfg.push_back(RealtimeSessionRenderer::Rack{graphsOwned[i].get(), sess.racks[i].id, 1.0f, sess.racks[i].muted, sess.racks[i].solo});
        srt.setXfaders(sess.xfaders, cfg);
      }
      // Build racks vector with per-rack gain (from session routes we still route to buses; use 1.0 here)
      std::vector<RealtimeSessionRenderer::Rack> rracks;
      rracks.reserve(graphsOwned.size());
      for (size_t i = 0; i < graphsOwned.size(); ++i) {
        rracks.push_back(RealtimeSessionRenderer::Rack{graphsOwned[i].get(), sess.racks[i].id, 1.0f, sess.racks[i].muted, sess.racks[i].solo});
      }
      srt.start(rracks, sess.buses, sess.routes, offlineSr > 0.0 ? offlineSr : 48000.0, 2);

      // Initial horizon: push one loop per rack (prefixed ids)
      {
        bool anySolo = false; for (const auto& rr : sess.racks) if (rr.solo) { anySolo = true; break; }
        std::vector<bool> activeFlags; activeFlags.reserve(sess.racks.size());
        for (const auto& rr : sess.racks) activeFlags.push_back(anySolo ? rr.solo : !rr.muted);
        for (size_t i = 0; i < rackRTs.size(); ++i) {
          if (i >= activeFlags.size() || !activeFlags[i]) continue;
          size_t pushed = 0;
          for (const auto& c : rackRTs[i].baseCmds) {
            Command cmd{}; cmd.sampleTime = c.sampleTime; cmd.nodeId = internNodeId(c.nodeId); if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger; else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam; else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp; cmd.paramId = c.paramId; cmd.value = c.value; cmd.rampMs = c.rampMs; (void)cmdQueue.push(cmd);
            ++pushed;
          }
          if (rtDebugSession) std::fprintf(stderr, "[rt-session] init enqueued rack=%s cmds=%zu\n", rackRTs[i].rackId.c_str(), pushed);
        }
      }

      // Feeder: extend horizon per rack
      std::thread sessionFeeder;
      sessionFeeder = std::thread([&cmdQueue, &rackRTs, &srt, rtDebugSession, sess]() mutable {
        const uint64_t desiredAhead = static_cast<uint64_t>(5.0 * srt.sampleRate());
        std::vector<uint64_t> nextOffset(rackRTs.size(), 0ull);
        for (size_t i = 0; i < rackRTs.size(); ++i) nextOffset[i] = rackRTs[i].loopLen;
        bool anySolo = false; for (const auto& rr : sess.racks) if (rr.solo) { anySolo = true; break; }
        std::vector<bool> activeFlags; activeFlags.reserve(sess.racks.size());
        for (const auto& rr : sess.racks) activeFlags.push_back(anySolo ? rr.solo : !rr.muted);
        while (gRunning.load()) {
          const uint64_t now = static_cast<uint64_t>(srt.sampleCounter());
          for (size_t i = 0; i < rackRTs.size(); ++i) {
            if (i >= activeFlags.size() || !activeFlags[i]) continue;
            if (rackRTs[i].loopLen == 0) continue;
            if (nextOffset[i] <= now + desiredAhead) {
              size_t pushed = 0;
              for (auto c : rackRTs[i].baseCmds) {
                Command cmd{}; cmd.sampleTime = c.sampleTime + nextOffset[i]; cmd.nodeId = internNodeId(c.nodeId); if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger; else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam; else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp; cmd.paramId = c.paramId; cmd.value = c.value; cmd.rampMs = c.rampMs;
                while (gRunning.load() && !cmdQueue.push(cmd)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                ++pushed;
                if (!gRunning.load()) break;
              }
              if (rtDebugSession) std::fprintf(stderr, "[rt-session] extend rack=%s offset=%llu pushed=%zu now=%llu\n", rackRTs[i].rackId.c_str(), (unsigned long long)nextOffset[i], pushed, (unsigned long long)now);
              nextOffset[i] += rackRTs[i].loopLen;
            }
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      });

      // Main loop waits for Enter/Ctrl-C (or session duration)
      while (gRunning.load()) {
        if (sess.durationSec > 0.0) {
          const double tNow = static_cast<double>(srt.sampleCounter()) / srt.sampleRate();
          if (tNow >= sess.durationSec) {
            if (sess.loop) {
              // Restart session: reset sample counter and restart command feeders
              std::fprintf(stderr, "[rt-session] looping back to start (duration=%.3f sec)\n", sess.durationSec);
              srt.resetSampleCounter();

              // Reset all rack graphs to initial state for seamless looping
              for (size_t i = 0; i < graphsOwned.size(); ++i) {
                if (graphsOwned[i]) {
                  graphsOwned[i]->reset();
                }
              }

              // Note: The feeder thread already handles looping by resetting nextOffset to loopLen
              // when it reaches the end, so we don't need to do anything special here
            } else {
              gRunning.store(false);
              break;
            }
          }
        }
        if (isStdinReady()) {
          char buf[4]; (void)read(STDIN_FILENO, buf, sizeof(buf)); gRunning.store(false); break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (sessionFeeder.joinable()) sessionFeeder.join();
      srt.stop();
      return 0;
    } catch (const std::exception& e) {
      std::fprintf(stderr, "Realtime session failed: %s\n", e.what());
      return 1;
    }
  } else {
        if (baseSpan >= horizonFrames) {
          realtimeCmds = baseCmds;
        } else {
          for (uint64_t offset = 0; offset < horizonFrames; offset += loopLen) {
            for (auto c : baseCmds) { c.sampleTime += offset; realtimeCmds.push_back(c); }
          }
        }
      }
      // Defensive stable sort to ensure deterministic same-sample ordering
      std::stable_sort(realtimeCmds.begin(), realtimeCmds.end(), [](const auto& a, const auto& b){
        if (a.sampleTime != b.sampleTime) return a.sampleTime < b.sampleTime;
        if (a.nodeId != b.nodeId) return a.nodeId < b.nodeId;
        if (a.type != b.type) return a.type < b.type;
        if (a.paramId != b.paramId) return a.paramId < b.paramId;
        return a.value < b.value;
      });
      // Trim only in non-feeder mode; with feeder we need the full first loop queued
      if (!startFeeder) {
        realtimeCmds.erase(std::remove_if(realtimeCmds.begin(), realtimeCmds.end(), [&](const auto& c){ return c.sampleTime >= horizonFrames; }), realtimeCmds.end());
      }
      // startFeeder already computed above
      if (!startFeeder) {
        for (const auto& c : realtimeCmds) {
          Command cmd{};
          cmd.sampleTime = c.sampleTime;
          cmd.nodeId = internNodeId(c.nodeId);
          if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger;
          else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam;
          else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp;
          cmd.paramId = c.paramId;
          cmd.value = c.value;
          cmd.rampMs = c.rampMs;
          while (gRunning.load() && !cmdQueue.push(cmd)) {
            if (rtDebugFeed) {
              std::fprintf(stderr, "[rt-feed] init queue full: size=%zu/%zu, cmd at %llu type=%u pid=%u\n",
                cmdQueue.approxSizeProducer(), cmdQueue.capacity(),
                static_cast<unsigned long long>(cmd.sampleTime),
                (unsigned)cmd.type, (unsigned)cmd.paramId);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
          if (!gRunning.load()) break;
        }
      }

      // Rolling feeder thread to extend horizon in realtime without flooding the queue
      if (startFeeder) {
        uint64_t nextOffset = loopLen; // after the first loop
        const uint64_t desiredAheadFrames = static_cast<uint64_t>(5.0 * rt.sampleRate()); // keep ~5s of commands ahead
        transportFeeder = std::thread([&cmdQueue, baseCmds, realtimeCmds, loopLen, nextOffset, &rt, desiredAheadFrames, rtDebugFeed]() mutable {
          uint64_t offset = nextOffset;
          // Initial bulk push is done here to keep SPSC contract (single producer)
          for (const auto& c : realtimeCmds) {
            Command cmd{};
            cmd.sampleTime = c.sampleTime;
            cmd.nodeId = internNodeId(c.nodeId);
            if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger;
            else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam;
            else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp;
            cmd.paramId = c.paramId;
            cmd.value = c.value;
            cmd.rampMs = c.rampMs;
            while (gRunning.load() && !cmdQueue.push(cmd)) {
              if (rtDebugFeed) {
                std::fprintf(stderr, "[rt-feed] init queue full: size=%zu/%zu, cmd at %llu type=%u pid=%u\n",
                  cmdQueue.approxSizeProducer(), cmdQueue.capacity(),
                  static_cast<unsigned long long>(cmd.sampleTime),
                  (unsigned)cmd.type, (unsigned)cmd.paramId);
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!gRunning.load()) break;
          }
          while (gRunning.load()) {
            const uint64_t framesNow = static_cast<uint64_t>(rt.sampleCounter());
            // Enqueue next loop when its start is within desiredAheadFrames from now
            if (offset <= framesNow + desiredAheadFrames) {
              // Enqueue one additional loop span
              size_t pushed = 0;
              for (auto c : baseCmds) {
                Command cmd{};
                cmd.sampleTime = c.sampleTime + offset;
                cmd.nodeId = internNodeId(c.nodeId);
                if (c.type == std::string("Trigger")) cmd.type = CommandType::Trigger;
                else if (c.type == std::string("SetParam")) cmd.type = CommandType::SetParam;
                else if (c.type == std::string("SetParamRamp")) cmd.type = CommandType::SetParamRamp;
                cmd.paramId = c.paramId;
                cmd.value = c.value;
                cmd.rampMs = c.rampMs;
                // Always push; if queue is saturated, retry briefly but allow shutdown to break out
                while (gRunning.load() && !cmdQueue.push(cmd)) {
                  if (rtDebugFeed) {
                    std::fprintf(stderr, "[rt-feed] queue full: size=%zu/%zu at f=%llu, cmd at %llu type=%u pid=%u\n",
                      cmdQueue.approxSizeProducer(), cmdQueue.capacity(),
                      static_cast<unsigned long long>(framesNow), static_cast<unsigned long long>(cmd.sampleTime),
                      (unsigned)cmd.type, (unsigned)cmd.paramId);
                  }
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                ++pushed;
                if (!gRunning.load()) break; // allow clean shutdown without blocking on a full queue
              }
              if (!gRunning.load()) break;
              if (rtDebugFeed) {
                const uint64_t framesAfter = static_cast<uint64_t>(rt.sampleCounter());
                std::fprintf(stderr, "[rt-feed] extended horizon offset=%llu (+%llu) now=%llu span=%llu pushed=%zu\n",
                  static_cast<unsigned long long>(offset), static_cast<unsigned long long>(loopLen),
                  static_cast<unsigned long long>(framesAfter), static_cast<unsigned long long>(loopLen), pushed);
              }
              offset += loopLen;
            } else {
              if (rtDebugFeed) {
                const uint64_t need = (offset + loopLen);
                std::fprintf(stderr, "[rt-feed] sleeping: now=%llu need<=%llu (ahead=%lld)\n",
                  static_cast<unsigned long long>(framesNow), static_cast<unsigned long long>(need),
                  static_cast<long long>((long long)need - (long long)framesNow));
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
          }
        });
      }
    } catch (...) {}
  }
  double elapsedSec = 0.0;
  uint64_t lastPrintedLoop = 0;
  while (gRunning.load()) {
    if (isStdinReady()) {
      char buf[4];
      (void)read(STDIN_FILENO, buf, sizeof(buf));
      gRunning.store(false);
      break;
    }
    // When --print-triggers is enabled, the render callback prints loop boundaries precisely.
    // Suppress the coarse main-thread loop print in that case to avoid late/duplicate messages.
    if (rtLoopLen > 0 && !printTriggers) {
      const uint64_t frames = static_cast<uint64_t>(rt.sampleCounter());
      const uint64_t loopIdx = frames / rtLoopLen;
      if (loopIdx > lastPrintedLoop) {
        if (verbose) {
          const double seconds = static_cast<double>(frames) / rt.sampleRate();
          std::fprintf(stderr, "Loop %llu at %s (%.3fs)\n",
                       static_cast<unsigned long long>(loopIdx), formatDuration(seconds).c_str(), seconds);
        }
        lastPrintedLoop = loopIdx;
        // Print estimated graph preroll (latency) once at first loop boundary
        if (loopIdx == 1 && !graphPath.empty()) {
          try {
            GraphSpec spec = loadGraphSpecFromJsonFile(graphPath);
            const uint64_t preroll = computeGraphPrerollSamples(spec, static_cast<uint32_t>(rt.sampleRate() + 0.5));
            std::fprintf(stderr, "Graph preroll: %.3f ms\n", 1000.0 * static_cast<double>(preroll) / rt.sampleRate());
          } catch (...) {}
        }
        if (cpuStats || cpuStatsPerNode) {
          const auto s = graph.getCpuSummary();
          std::fprintf(stderr, "CPU block avg=%.3fms max=%.3fms (avg=%.1f%% max=%.1f%%) xruns=%llu blocks=%llu\n",
                       s.avgMs, s.maxMs, s.avgPercent, s.maxPercent,
                       static_cast<unsigned long long>(s.overruns), static_cast<unsigned long long>(s.blocks));
          if (cpuStatsPerNode) {
            const auto per = graph.getPerNodeCpu();
            for (const auto& n : per) std::fprintf(stderr, "  %s: avg=%.1fus max=%.1fus\n", n.id.c_str(), n.avgUs, n.maxUs);
          }
        }
        if (metersPerNode) {
          const auto meters = graph.getNodeMeters(2);
          for (const auto& m : meters) {
            const bool inactive = (!std::isfinite(m.peakDb) && !std::isfinite(m.rmsDb));
            if (inactive) {
              std::fprintf(stderr, "  Node %s: inactive\n", m.id.c_str());
            } else {
              std::fprintf(stderr, "  Node %s: peak=%.2f dBFS rms=%.2f dBFS\n", m.id.c_str(), m.peakDb, m.rmsDb);
            }
          }
        }
      }
    }
    if (quitAfterSec > 0.0) {
      elapsedSec += 0.05;
      if (elapsedSec >= quitAfterSec) {
        gRunning.store(false);
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  rt.stop();
  if (metersPerNode) {
    const auto meters = graph.getNodeMeters(2);
    for (const auto& m : meters) {
      std::fprintf(stderr, "Node %s: peak=%.2f dBFS rms=%.2f dBFS\n", m.id.c_str(), m.peakDb, m.rmsDb);
    }
  }
  if (transportFeeder.joinable()) transportFeeder.join();

  return 0;
}


