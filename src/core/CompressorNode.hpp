#pragma once

#include "Node.hpp"
#include <algorithm>
#include <cmath>

class CompressorNode : public Node {
public:
  // Parameters
  float thresholdDb = -18.0f;  // dBFS
  float ratio = 2.0f;          // >= 1.0
  float attackMs = 10.0f;
  float releaseMs = 100.0f;
  float makeupDb = 0.0f;

  const char* name() const override { return "compressor"; }
  void prepare(double sampleRate, uint32_t /*maxBlock*/) override {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    updateCoefs();
    env_ = 0.0f;
  }
  void reset() override { env_ = 0.0f; }
  void process(ProcessContext ctx, float* interleavedOut, uint32_t channels) override {
    // Generator-style not used; compressor is insert, so this is a no-op
    (void)ctx; (void)interleavedOut; (void)channels;
  }
  void processInPlace(ProcessContext ctx, float* interleaved, uint32_t channels) override {
    // Fallback: compress main signal with itself as detector
    applySidechain(ctx, interleaved, interleaved, channels);
  }

  virtual void applySidechain(ProcessContext ctx, float* mainInterleaved, const float* scInterleaved, uint32_t channels) {
    const uint32_t frames = ctx.frames;
    const float thrLin = std::pow(10.0f, thresholdDb / 20.0f);
    const float makeupLin = std::pow(10.0f, makeupDb / 20.0f);
    if (channels == 0 || frames == 0) return;
    for (uint32_t i = 0; i < frames; ++i) {
      // detector: mono from sidechain by averaging
      float sc = 0.0f;
      if (channels == 1) {
        sc = std::fabs(scInterleaved[i]);
      } else {
        const float L = scInterleaved[2 * static_cast<size_t>(i)];
        const float R = scInterleaved[2 * static_cast<size_t>(i) + 1];
        sc = 0.5f * (std::fabs(L) + std::fabs(R));
      }
      // envelope follower
      const float target = sc;
      const float coef = (target > env_) ? attackCoef_ : releaseCoef_;
      env_ = target + coef * (env_ - target);
      // static curve
      float gain = 1.0f;
      if (env_ > thrLin && ratio > 1.0f) {
        const float envDb = 20.0f * std::log10(std::max(env_, 1e-8f));
        const float over = envDb - thresholdDb;
        const float grDb = -over * (1.0f - 1.0f / ratio);
        gain = std::pow(10.0f, grDb / 20.0f);
      }
      gain *= makeupLin;
      if (channels == 1) {
        mainInterleaved[i] *= gain;
      } else {
        mainInterleaved[2 * static_cast<size_t>(i)] *= gain;
        mainInterleaved[2 * static_cast<size_t>(i) + 1] *= gain;
      }
    }
  }

  void setParams(float thrDb, float rat, float attMs, float relMs, float mkDb) {
    thresholdDb = thrDb; ratio = std::max(1.0f, rat);
    attackMs = std::max(0.1f, attMs); releaseMs = std::max(0.1f, relMs); makeupDb = mkDb; updateCoefs();
  }

protected:
  void updateCoefs() {
    const float attT = std::max(0.0001f, attackMs / 1000.0f);
    const float relT = std::max(0.0001f, releaseMs / 1000.0f);
    attackCoef_ = std::exp(-1.0f / static_cast<float>(sampleRate_ * attT));
    releaseCoef_ = std::exp(-1.0f / static_cast<float>(sampleRate_ * relT));
  }

  double sampleRate_ = 48000.0;
  float env_ = 0.0f;
  float attackCoef_ = 0.0f;
  float releaseCoef_ = 0.0f;
};


