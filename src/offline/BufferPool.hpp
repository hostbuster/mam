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
  // Optional lifetimeId tags the buffer for precise release when its last consumer finishes.
  std::vector<float>& acquire(uint32_t frames, uint64_t lifetimeId = 0) {
    const size_t need = static_cast<size_t>(frames) * channels_;
    // find a free buffer large enough
    for (auto& e : entries_) {
      if (!e.inUse && e.data.size() >= need) { reuse(e, need, lifetimeId); return e.data; }
    }
    // else grow or create
    entries_.push_back(Entry{});
    entries_.back().data.assign(need, 0.0f);
    entries_.back().inUse = true;
    entries_.back().lifetimeId = lifetimeId;
    return entries_.back().data;
  }

  void releaseAll() {
    for (auto& e : entries_) e.inUse = false;
  }

  void release(float* ptr) {
    if (!ptr) return;
    for (auto& e : entries_) {
      if (!e.data.empty() && e.data.data() == ptr) { e.inUse = false; e.lifetimeId = 0; break; }
    }
  }

  void releaseById(uint64_t lifetimeId) {
    if (lifetimeId == 0) return;
    for (auto& e : entries_) {
      if (e.inUse && e.lifetimeId == lifetimeId) { e.inUse = false; e.lifetimeId = 0; break; }
    }
  }

private:
  struct Entry { std::vector<float> data; bool inUse = false; uint64_t lifetimeId = 0; };
  std::vector<Entry> entries_{};
  uint32_t channels_ = 2;

  static void reuse(Entry& e, size_t need, uint64_t lifetimeId) {
    if (e.data.size() < need) e.data.resize(need);
    std::fill(e.data.begin(), e.data.end(), 0.0f);
    e.inUse = true;
    e.lifetimeId = lifetimeId;
  }
};


