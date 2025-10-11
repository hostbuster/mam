#pragma once

#include <random>

// Simple global RNG for deterministic behavior when a seed is provided.
// Not thread-safe for concurrent mutation; seed once on startup.
inline std::mt19937& globalRng() {
  static std::mt19937 rng{std::random_device{}()};
  return rng;
}

inline void setGlobalSeed(uint32_t seed) {
  if (seed != 0) globalRng().seed(seed);
}


