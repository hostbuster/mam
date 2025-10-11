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
  std::fprintf(stderr, "Connections (%zu):\n", spec.connections.size());
  for (const auto& c : spec.connections) {
    std::fprintf(stderr, "  %s -> %s  wet=%g%% dry=%g%% ports %u->%u\n",
                 c.from.c_str(), c.to.c_str(), c.gainPercent, c.dryPercent, c.fromPort, c.toPort);
  }
}


