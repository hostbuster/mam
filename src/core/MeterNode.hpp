#pragma once

#include "Node.hpp"
#include <atomic>

// MeterNode: pass-through node that measures peak/RMS for observability.
class MeterNode final : public Node {
public:
  explicit MeterNode(const std::string& targetId) : targetId_(targetId) {}
  const char* name() const override { return "MeterNode"; }
  void prepare(double sampleRate, uint32_t maxBlock) override { (void)sampleRate; (void)maxBlock; }
  void reset() override {
    peak_.store(0.0); rms_.store(0.0);
  }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    (void)ctx;
    double p = 0.0, r = 0.0;
    measurePeakRms(interleavedOut, ctx.frames, channels, p, r);
    peak_.store(p); rms_.store(r);
  }
  void handleEvent(const Command&) override {}

  // Readbacks (non-RT)
  double peak() const { return peak_.load(); }
  double rms() const { return rms_.load(); }
  const std::string& targetId() const { return targetId_; }
  void updateFromBuffer(const float* interleaved, uint32_t frames, uint32_t channels) {
    double p = 0.0, r = 0.0; measurePeakRms(interleaved, frames, channels, p, r); peak_.store(p); rms_.store(r);
  }

private:
  std::string targetId_;
  std::atomic<double> peak_{0.0};
  std::atomic<double> rms_{0.0};
};


