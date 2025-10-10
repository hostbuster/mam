#pragma once

#include <cstdint>
#include <string>

struct ProcessContext {
  double sampleRate = 48000.0;
  uint32_t frames = 0;
};

class Node {
public:
  virtual ~Node() = default;
  virtual const char* name() const = 0;
  virtual void prepare(double sampleRate, uint32_t maxBlock) = 0;
  virtual void reset() = 0;
  virtual void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) = 0;
};



