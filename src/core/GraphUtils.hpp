#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <cstdio>
#include <nlohmann/json.hpp>
#include "GraphConfig.hpp"

inline void printTopoOrderFromSpec(const GraphSpec& spec);
inline uint64_t computeGraphPrerollSamples(const GraphSpec& spec, uint32_t sampleRate);
inline void printConnectionsSummary(const GraphSpec& spec);
inline void printPortsSummary(const GraphSpec& spec);

// Inline impls
inline void printTopoOrderFromSpec(const GraphSpec& spec) {
  if (spec.connections.empty()) {
    std::fprintf(stderr, "Topo order (insertion, %zu): ", spec.nodes.size());
    for (size_t i = 0; i < spec.nodes.size(); ++i) {
      std::fprintf(stderr, "%s%s", spec.nodes[i].id.c_str(), (i + 1 < spec.nodes.size() ? " -> " : "\n"));
    }
    return;
  }
  std::unordered_map<std::string,int> indeg;
  std::unordered_multimap<std::string,std::string> adj;
  for (const auto& n : spec.nodes) indeg[n.id] = 0;
  for (const auto& e : spec.connections) { if (indeg.count(e.to)) indeg[e.to]++; adj.emplace(e.from, e.to); }
  std::vector<std::string> q; q.reserve(indeg.size());
  for (const auto& kv : indeg) if (kv.second==0) q.push_back(kv.first);
  std::vector<std::string> order; order.reserve(indeg.size());
  for (size_t qi=0; qi<q.size(); ++qi) {
    const auto u = q[qi]; order.push_back(u);
    auto range = adj.equal_range(u);
    for (auto it = range.first; it != range.second; ++it) {
      auto& v = it->second; if (--indeg[v]==0) q.push_back(v);
    }
  }
  if (!order.empty()) {
    std::fprintf(stderr, "Topo order (%zu): ", order.size());
    for (size_t i=0;i<order.size();++i) std::fprintf(stderr, "%s%s", order[i].c_str(), (i+1<order.size()?" -> ":"\n"));
  }
}

inline uint64_t computeGraphPrerollSamples(const GraphSpec& spec, uint32_t sampleRate) {
  std::unordered_map<std::string, uint32_t> nodeLatency;
  for (const auto& n : spec.nodes) {
    uint32_t lat = 0;
    if (n.type == std::string("delay")) {
      try {
        nlohmann::json j = nlohmann::json::parse(n.paramsJson);
        const double ms = j.value("delayMs", 0.0);
        lat = static_cast<uint32_t>(ms * static_cast<double>(sampleRate) * 0.001 + 0.5);
      } catch (...) {}
    }
    nodeLatency[n.id] = lat;
  }
  std::unordered_map<std::string, double> acc;
  std::unordered_map<std::string, int> indeg;
  std::unordered_multimap<std::string, std::string> adj;
  for (const auto& n : spec.nodes) { indeg[n.id] = 0; acc[n.id] = static_cast<double>(nodeLatency[n.id]); }
  for (const auto& e : spec.connections) { if (indeg.count(e.to)) indeg[e.to]++; adj.emplace(e.from, e.to); }
  std::vector<std::string> q; q.reserve(indeg.size());
  for (const auto& kv : indeg) if (kv.second == 0) q.push_back(kv.first);
  for (size_t qi = 0; qi < q.size(); ++qi) {
    const auto u = q[qi];
    auto range = adj.equal_range(u);
    for (auto it = range.first; it != range.second; ++it) {
      const auto& v = it->second;
      const double cand = acc[u] + static_cast<double>(nodeLatency[v]);
      if (cand > acc[v]) acc[v] = cand;
      if (--indeg[v] == 0) q.push_back(v);
    }
  }
  double maxS = 0.0; for (const auto& kv : acc) if (kv.second > maxS) maxS = kv.second;
  return static_cast<uint64_t>(maxS + 0.5);
}

inline void printConnectionsSummary(const GraphSpec& spec) {
  if (spec.connections.empty()) {
    std::fprintf(stderr, "No connections defined.\n");
    return;
  }
  // Build port channel maps if declared
  std::unordered_map<std::string, std::unordered_map<uint32_t, uint32_t>> inCh, outCh;
  for (const auto& n : spec.nodes) {
    if (n.ports.has) {
      for (const auto& ip : n.ports.inputs) inCh[n.id][ip.index] = ip.channels;
      for (const auto& op : n.ports.outputs) outCh[n.id][op.index] = op.channels;
    }
  }
  std::fprintf(stderr, "Connections (%zu):\n", spec.connections.size());
  for (const auto& c : spec.connections) {
    uint32_t fromCh = 0, toCh = 0;
    if (outCh.count(c.from) && outCh[c.from].count(c.fromPort)) fromCh = outCh[c.from][c.fromPort];
    if (inCh.count(c.to) && inCh[c.to].count(c.toPort)) toCh = inCh[c.to][c.toPort];
    if (fromCh == 0 && toCh == 0) {
      std::fprintf(stderr, "  %s -> %s  wet=%g%% dry=%g%% ports %u->%u\n",
                   c.from.c_str(), c.to.c_str(), c.gainPercent, c.dryPercent, c.fromPort, c.toPort);
    } else {
      std::fprintf(stderr, "  %s -> %s  wet=%g%% dry=%g%% ports %u(ch%u)->%u(ch%u)\n",
                   c.from.c_str(), c.to.c_str(), c.gainPercent, c.dryPercent, c.fromPort, fromCh, c.toPort, toCh);
    }
  }
}

inline void printPortsSummary(const GraphSpec& spec) {
  if (spec.nodes.empty()) return;
  std::fprintf(stderr, "Ports per node:\n");
  for (const auto& n : spec.nodes) {
    if (!n.ports.has) continue;
    std::fprintf(stderr, "  %s (%s)\n", n.id.c_str(), n.type.c_str());
    if (!n.ports.inputs.empty()) {
      std::fprintf(stderr, "    inputs: ");
      for (size_t i=0;i<n.ports.inputs.size();++i) {
        const auto& p = n.ports.inputs[i];
        if (p.channels > 0) std::fprintf(stderr, "%u:%s:%s:ch%u%s", p.index, p.type.c_str(), p.role.empty()?"main":p.role.c_str(), p.channels, (i+1<n.ports.inputs.size()?", ":""));
        else std::fprintf(stderr, "%u:%s:%s%s", p.index, p.type.c_str(), p.role.empty()?"main":p.role.c_str(), (i+1<n.ports.inputs.size()?", ":""));
      }
      std::fprintf(stderr, "\n");
    }
    if (!n.ports.outputs.empty()) {
      std::fprintf(stderr, "    outputs: ");
      for (size_t i=0;i<n.ports.outputs.size();++i) {
        const auto& p = n.ports.outputs[i];
        if (p.channels > 0) std::fprintf(stderr, "%u:%s:%s:ch%u%s", p.index, p.type.c_str(), p.role.empty()?"main":p.role.c_str(), p.channels, (i+1<n.ports.outputs.size()?", ":""));
        else std::fprintf(stderr, "%u:%s:%s%s", p.index, p.type.c_str(), p.role.empty()?"main":p.role.c_str(), (i+1<n.ports.outputs.size()?", ":""));
      }
      std::fprintf(stderr, "\n");
    }
  }
}


