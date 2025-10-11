#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>

// Simple header-only buffer pool for offline rendering to avoid repeat allocations.
// Not realtime-thread safe; intended for offline schedulers.
class BufferPool {
public:
  explicit BufferPool(uint32_t channels) : channels_(channels) {}

  void setChannels(uint32_t channels) { channels_ = channels; }

  // Acquire a buffer with at least frames*channels samples, zeroed.
  std::vector<float>& acquire(uint32_t frames) {
    const size_t need = static_cast<size_t>(frames) * channels_;
    // find a free buffer large enough
    for (auto& e : entries_) {
      if (!e.inUse && e.data.size() >= need) { reuse(e, need); return e.data; }
    }
    // else grow or create
    entries_.push_back(Entry{});
    entries_.back().data.assign(need, 0.0f);
    entries_.back().inUse = true;
    return entries_.back().data;
  }

  void releaseAll() {
    for (auto& e : entries_) e.inUse = false;
  }

private:
  struct Entry { std::vector<float> data; bool inUse = false; };
  std::vector<Entry> entries_{};
  uint32_t channels_ = 2;

  static void reuse(Entry& e, size_t need) {
    if (e.data.size() < need) e.data.resize(need);
    std::fill(e.data.begin(), e.data.end(), 0.0f);
    e.inUse = true;
  }
};


