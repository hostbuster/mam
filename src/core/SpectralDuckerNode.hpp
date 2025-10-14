#pragma once

#include "CompressorNode.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

// Minimal multiband spectral ducking built on CompressorNode plumbing.
// Uses 3 peaking filters as approximate bands and applies per-band envelope gain.
class SpectralDuckerNode : public CompressorNode {
public:
  enum class ApplyMode { Multiply, DynamicEq };
  enum class StereoMode { LR, MidSide };
  struct Band {
    float centerHz = 100.0f;
    float q = 1.0f;
    float depthDb = -6.0f;    // max attenuation
    float thresholdDb = -18.0f; // detector threshold (per-band)
    float ratio = 2.0f;       // >=1
    float kneeDb = 6.0f;      // soft knee width
    float holdMs = 0.0f;      // hold time after GR starts
  };

  // Parameters
  float lookaheadMs = 5.0f; // reserved for future use
  float mix = 1.0f;         // 0..1
  float scHpfHz = 0.0f;     // detector high-pass
  ApplyMode applyMode = ApplyMode::Multiply;
  StereoMode stereoMode = StereoMode::LR;
  float msSideScale = 0.5f; // proportion of ducking applied to Side when in MidSide mode
  std::vector<Band> bands{{60.0f,1.0f,-9.0f},{120.0f,1.0f,-6.0f},{250.0f,0.8f,-3.0f}};

  const char* name() const override { return "spectral_ducker"; }

  void prepare(double sampleRate, uint32_t maxBlock) override {
    CompressorNode::prepare(sampleRate, maxBlock);
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;
    maxBlock_ = maxBlock;
    lookaheadSamples_ = static_cast<uint32_t>(std::max(0.0, std::floor((lookaheadMs / 1000.0f) * static_cast<float>(sampleRate_) + 0.5)));
    ensureDelayCapacity(lastChannels_ == 0 ? 2u : lastChannels_);
    updateDetectorHpf();
    setupBands();
  }
  void reset() override {
    CompressorNode::reset();
    for (auto& s : states_) s = BiquadState{};
    for (auto& e : envs_) e = 0.0f;
    std::fill(delay_.begin(), delay_.end(), 0.0f);
    writeIndexFrames_ = 0;
    scPrevX_ = 0.0f; scPrevY_ = 0.0f;
    for (auto& perBand : mainStates_) for (auto& st : perBand) st = BiquadState{};
    std::fill(lastBandGain_.begin(), lastBandGain_.end(), 1.0f);
    std::fill(holdRemain_.begin(), holdRemain_.end(), 0u);
  }

  void applySidechain(ProcessContext ctx, float* mainInterleaved, const float* scInterleaved, uint32_t channels) override {
    const uint32_t frames = ctx.frames;
    if (channels == 0 || frames == 0) return;
    if (lastChannels_ != channels || delay_.empty()) ensureDelayCapacity(channels);
    lastChannels_ = channels;
    // Build mono sidechain
    scMono_.assign(frames, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
      double sum = 0.0;
      for (uint32_t c = 0; c < channels; ++c) sum += scInterleaved[static_cast<size_t>(i)*channels + c];
      scMono_[i] = static_cast<float>(sum / static_cast<double>(channels));
    }
    // Optional detector HPF
    if (scHpfHz > 1.0f && scHpfAlpha_ > 0.0f) {
      for (uint32_t i = 0; i < frames; ++i) {
        const float x = scMono_[i];
        const float y = scHpfAlpha_ * (scPrevY_ + x - scPrevX_);
        scPrevX_ = x; scPrevY_ = y; scMono_[i] = y;
      }
    }
    // For each band: filter detector -> envelope -> per-band gain via threshold/ratio/knee/hold; combine via min
    if (filters_.size() != bands.size()) setupBands();
    if (envs_.size() != bands.size()) envs_.assign(bands.size(), 0.0f);
    if (states_.size() != bands.size()) states_.assign(bands.size(), BiquadState{});
    if (lastBandGain_.size() != bands.size()) lastBandGain_.assign(bands.size(), 1.0f);
    if (holdRemain_.size() != bands.size()) holdRemain_.assign(bands.size(), 0u);

    gains_.assign(frames, 1.0f);
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      const Band& b = bands[bi];
      const float depthLin = std::pow(10.0f, b.depthDb / 20.0f);
      const Biquad& q = filters_[bi];
      BiquadState& st = states_[bi];
      float env = envs_[bi];
      uint32_t hold = holdRemain_[bi];
      for (uint32_t i = 0; i < frames; ++i) {
        const float rect = std::fabs(q.process(scMono_[i], st));
        const float coef = (rect > env) ? attackCoef_ : releaseCoef_;
        env = rect + coef * (env - rect);
        float g = 1.0f;
        // Convert to dB for static curve
        const float envDb = (env > 1e-8f) ? 20.0f * std::log10(env) : -80.0f;
        const float knee = std::max(0.0f, b.kneeDb);
        const float thr = b.thresholdDb;
        const float over = envDb - thr;
        if (hold > 0) {
          g = lastBandGain_[bi];
          hold--;
        } else if (over > -0.5f * knee) {
          float effRatio = b.ratio;
          if (knee > 0.0f) {
            const float t = std::clamp((over + 0.5f * knee) / knee, 0.0f, 1.0f);
            effRatio = 1.0f + (b.ratio - 1.0f) * t;
          }
          if (effRatio > 1.0f) {
            const float grDb = -std::max(0.0f, over) * (1.0f - 1.0f / effRatio);
            g = std::pow(10.0f, grDb / 20.0f);
          }
          // Hold if we engaged GR
          if (g < 1.0f && b.holdMs > 0.0f) {
            hold = holdSamplesPerBand_[bi];
            lastBandGain_[bi] = g;
          }
        }
        // Clamp by depth
        g = std::max(depthLin, g);
        gains_[i] = std::min(gains_[i], g);
      }
      envs_[bi] = env;
      holdRemain_[bi] = hold;
    }
    // Apply per-sample band-combined gain to main signal (two modes)
    const float wet = std::clamp(mix, 0.0f, 1.0f);
    const float dry = 1.0f - wet;
    if (applyMode == ApplyMode::Multiply) {
      for (uint32_t i = 0; i < frames; ++i) {
        const float g = gains_[i] * wet + dry;
        const uint32_t writeF = static_cast<uint32_t>(writeIndexFrames_ % capacityFrames_);
        const uint32_t readF  = (writeIndexFrames_ + capacityFrames_ - std::min(lookaheadSamples_, capacityFrames_ - 1)) % capacityFrames_;
        if (channels == 2 && stereoMode == StereoMode::MidSide) {
          const size_t wiL = static_cast<size_t>(writeF) * 2;
          const size_t wiR = wiL + 1;
          const size_t riL = static_cast<size_t>(readF) * 2;
          const size_t riR = riL + 1;
          // write
          delay_[wiL] = mainInterleaved[static_cast<size_t>(i)*2];
          delay_[wiR] = mainInterleaved[static_cast<size_t>(i)*2 + 1];
          // read delayed
          const float xL = delay_[riL];
          const float xR = delay_[riR];
          float M = 0.5f * (xL + xR);
          float S = 0.5f * (xL - xR);
          const float gMid = g;
          const float gSide = 1.0f - (1.0f - g) * std::clamp(msSideScale, 0.0f, 1.0f);
          M *= gMid; S *= gSide;
          const float yL = M + S;
          const float yR = M - S;
          mainInterleaved[static_cast<size_t>(i)*2]     = yL;
          mainInterleaved[static_cast<size_t>(i)*2 + 1] = yR;
        } else {
          for (uint32_t c = 0; c < channels; ++c) {
            const size_t wi = static_cast<size_t>(writeF) * channels + c;
            const size_t ri = static_cast<size_t>(readF) * channels + c;
            delay_[wi] = mainInterleaved[static_cast<size_t>(i)*channels + c];
            float x = delay_[ri];
            mainInterleaved[static_cast<size_t>(i)*channels + c] = x * g;
          }
        }
        writeIndexFrames_++;
      }
    } else { // DynamicEq
      ensureMainStates(channels);
      for (uint32_t i = 0; i < frames; ++i) {
        const uint32_t writeF = static_cast<uint32_t>(writeIndexFrames_ % capacityFrames_);
        const uint32_t readF  = (writeIndexFrames_ + capacityFrames_ - std::min(lookaheadSamples_, capacityFrames_ - 1)) % capacityFrames_;
        for (uint32_t c = 0; c < channels; ++c) {
          const size_t wi = static_cast<size_t>(writeF) * channels + c;
          const size_t ri = static_cast<size_t>(readF) * channels + c;
          delay_[wi] = mainInterleaved[static_cast<size_t>(i)*channels + c];
          float x = delay_[ri];
          float adj = 0.0f;
          for (size_t bi = 0; bi < bands.size(); ++bi) {
            const auto& q = filters_[bi];
            auto& st = mainStates_[bi][c];
            const float bp = q.process(x, st);
            const float depthLin = std::pow(10.0f, bands[bi].depthDb / 20.0f);
            const float gBand = (gains_[i] <= 1.0f) ? std::max(depthLin, gains_[i]) : 1.0f; // approximate per-band
            adj += (gBand - 1.0f) * bp;
          }
          const float y = x + adj;
          mainInterleaved[static_cast<size_t>(i)*channels + c] = y * wet + x * dry;
        }
        writeIndexFrames_++;
      }
    }
  }

  uint32_t latencySamples() const override { return lookaheadSamples_; }

private:
  // Simple biquad peaking EQ structure
  struct BiquadState { float z1=0,z2=0; };
  struct Biquad { float b0=1,b1=0,b2=0,a1=0,a2=0; float process(float x, BiquadState& s) const; };
  static Biquad designPeaking(float sampleRate, float centerHz, float q, float gainDb) {
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * (centerHz / sampleRate);
    const float cosw = std::cos(w0), sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * q);
    float b0 = 1 + alpha * A;
    float b1 = -2 * cosw;
    float b2 = 1 - alpha * A;
    float a0 = 1 + alpha / A;
    float a1 = -2 * cosw;
    float a2 = 1 - alpha / A;
    Biquad biq{}; biq.b0 = b0/a0; biq.b1 = b1/a0; biq.b2 = b2/a0; biq.a1 = a1/a0; biq.a2 = a2/a0; return biq;
  }
  static Biquad designBandpass(float sampleRate, float centerHz, float q) {
    const float w0 = 2.0f * static_cast<float>(M_PI) * (centerHz / sampleRate);
    const float cosw = std::cos(w0), sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * q);
    float b0 = alpha, b1 = 0, b2 = -alpha;
    float a0 = 1 + alpha; float a1 = -2 * cosw; float a2 = 1 - alpha;
    Biquad biq{}; biq.b0 = b0/a0; biq.b1 = b1/a0; biq.b2 = b2/a0; biq.a1 = a1/a0; biq.a2 = a2/a0; return biq;
  }
  void setupBands() {
    filters_.clear(); filters_.reserve(bands.size());
    for (const auto& b : bands) filters_.push_back(designBandpass(static_cast<float>(sampleRate_), b.centerHz, std::max(0.1f, b.q)));
    states_.assign(bands.size(), BiquadState{});
    envs_.assign(bands.size(), 0.0f);
    lastBandGain_.assign(bands.size(), 1.0f);
    holdRemain_.assign(bands.size(), 0u);
    holdSamplesPerBand_.assign(bands.size(), 0u);
    for (size_t i = 0; i < bands.size(); ++i) {
      const float ms = std::max(0.0f, bands[i].holdMs);
      holdSamplesPerBand_[i] = static_cast<uint32_t>(std::floor((ms / 1000.0f) * sampleRate_ + 0.5));
    }
  }

  void ensureDelayCapacity(uint32_t channels) {
    if (channels == 0) channels = 2;
    capacityFrames_ = std::max<uint32_t>(lookaheadSamples_ + std::max<uint32_t>(maxBlock_, 1u), 2u);
    delay_.assign(static_cast<size_t>(capacityFrames_) * channels, 0.0f);
    writeIndexFrames_ = 0;
  }
  void ensureMainStates(uint32_t channels) {
    if (mainStates_.size() != bands.size()) mainStates_.assign(bands.size(), std::vector<BiquadState>());
    for (auto& v : mainStates_) if (v.size() != channels) v.assign(channels, BiquadState{});
  }
  void updateDetectorHpf() {
    if (scHpfHz > 1.0f) {
      const double dt = 1.0 / std::max(1.0, sampleRate_);
      const double RC = 1.0 / (2.0 * M_PI * static_cast<double>(scHpfHz));
      scHpfAlpha_ = static_cast<float>(RC / (RC + dt));
    } else scHpfAlpha_ = 0.0f;
  }

  double sampleRate_ = 48000.0;
  uint32_t maxBlock_ = 0;
  std::vector<float> scMono_{};
  std::vector<float> gains_{};
  std::vector<Biquad> filters_{};
  std::vector<BiquadState> states_{};
  std::vector<float> envs_{};
  // Lookahead delay line (interleaved)
  std::vector<float> delay_{};
  uint32_t lookaheadSamples_ = 0;
  uint32_t capacityFrames_ = 0;
  uint64_t writeIndexFrames_ = 0;
  uint32_t lastChannels_ = 0;
  // Dynamic EQ state (per band x channel)
  std::vector<std::vector<BiquadState>> mainStates_{};
  // Detector HPF state
  float scPrevX_ = 0.0f, scPrevY_ = 0.0f;
  float scHpfAlpha_ = 0.0f;
  // Per-band hold and last gain
  std::vector<float> lastBandGain_{};
  std::vector<uint32_t> holdRemain_{};
  std::vector<uint32_t> holdSamplesPerBand_{};
};

inline float SpectralDuckerNode::Biquad::process(float x, SpectralDuckerNode::BiquadState& s) const {
  const float y = b0*x + b1*s.z1 + b2*s.z2 - a1*s.z1 - a2*s.z2; // Direct Form I (simplified)
  s.z2 = s.z1; s.z1 = y;
  return y;
}


