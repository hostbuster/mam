#pragma once

#include "Node.hpp"
#include "../io/AudioFileWriter.hpp"
#include <vector>
#include <string>

// WiretapNode: pass-through insert that records its input to a file (offline-safe)
class WiretapNode : public Node {
public:
  explicit WiretapNode(std::string path, bool enabled = true)
    : path_(std::move(path)), enabled_(enabled) {}

  ~WiretapNode() override {
    flush();
  }

  const char* name() const override { return "wiretap"; }

  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    outTap_.clear(); channels_ = 0; wrote_ = false;
  }

  void reset() override {
    // no-op: we flush late to capture whole render
  }

  void process(ProcessContext, float*, uint32_t) override {}

  void processInPlace(ProcessContext ctx, float* interleaved, uint32_t channels) override {
    if (!enabled_ || path_.empty()) return;
    if (channels_ == 0) channels_ = channels;
    const size_t n = static_cast<size_t>(ctx.frames) * channels;
    const size_t prev = outTap_.size();
    outTap_.resize(prev + n);
    std::memcpy(outTap_.data() + prev, interleaved, n * sizeof(float));
  }

  void flush() {
    if (!enabled_ || path_.empty() || outTap_.empty() || wrote_) return;
    AudioFileSpec spec; spec.format = FileFormat::Wav; spec.bitDepth = BitDepth::Float32; spec.sampleRate = static_cast<uint32_t>(sampleRate_ + 0.5); spec.channels = channels_ > 0 ? channels_ : 2;
    try { writeWithExtAudioFile(path_, spec, outTap_); wrote_ = true; } catch (...) { /* swallow for debug use */ }
  }

private:
  std::string path_;
  bool enabled_ = true;
  double sampleRate_ = 48000.0;
  uint32_t channels_ = 0;
  std::vector<float> outTap_{};
  bool wrote_ = false;
};


