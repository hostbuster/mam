#pragma once

#include <cstdint>
#include <atomic>
#include <vector>
#include <cstring>

using SampleTime = uint64_t;
using NodeIdStr = const char*; // stored elsewhere; commands copy into a small buffer if needed

enum class CommandType : uint8_t { Trigger = 0, SetParam, SetParamRamp };

struct Command {
  SampleTime sampleTime = 0;  // absolute
  NodeIdStr nodeId = nullptr; // pointer to stable id string
  CommandType type = CommandType::Trigger;
  uint16_t paramId = 0;       // for SetParam/SetParamRamp
  float value = 0.0f;         // new value
  float rampMs = 0.0f;        // for SetParamRamp
  const char* paramNameStr = nullptr; // optional diagnostics fallback for printing
  uint8_t source = 0;         // 0 = rack (graph/transport), 1 = session
};

template <size_t Capacity>
class SpscCommandQueue {
public:
  SpscCommandQueue() : head_(0), tail_(0) {}

  bool push(const Command& c) {
    const size_t next = (head_ + 1) % Capacity;
    if (next == tail_.load(std::memory_order_acquire)) return false; // full
    buffer_[head_] = c;
    head_ = next;
    return true;
  }

  // Producer-side approximate size and capacity (for debugging/telemetry only)
  size_t approxSizeProducer() const {
    const size_t tail = tail_.load(std::memory_order_acquire);
    const size_t head = head_;
    return (head >= tail) ? (head - tail) : (Capacity - (tail - head));
  }
  constexpr size_t capacity() const { return Capacity - 1; }

  // Drain into out vector all commands with sampleTime < cutoff
  void drainUpTo(SampleTime cutoff, std::vector<Command>& out) {
    while (tail_.load(std::memory_order_relaxed) != head_) {
      const Command& c = buffer_[tail_];
      if (c.sampleTime >= cutoff) break;
      out.push_back(c);
      tail_.store((tail_.load(std::memory_order_relaxed) + 1) % Capacity, std::memory_order_release);
    }
  }

private:
  Command buffer_[Capacity];
  size_t head_;
  std::atomic<size_t> tail_;
};


