#pragma once

#include <cstdint>
#include <cmath>

struct Tb303ExtParams {
  int waveform = 0;          // 0=saw, 1=square
  float tuneSemitones = 0.0f;
  float glideMs = 10.0f;
  float cutoffHz = 800.0f;
  float resonance = 0.3f;    // 0..1
  float envMod = 0.5f;       // filter env amount (0..1)
  // Legacy decay-only mode (default)
  float filterDecayMs = 200.0f;
  float ampDecayMs = 200.0f;
  float ampGain = 0.8f;
  float accent = 0.0f;       // 0..1
  float velocity = 1.0f;     // 0..1
  float noteSemitones = 48.0f; // C3 default
  float drive = 0.0f;        // 0..1 (pre-filter soft drive)
  // Optional ADSR mode (ENV_MODE > 0.5 enables these)
  float envMode = 0.0f;      // 0 = decay-only (legacy), 1 = ADSR
  float filterAttackMs = 0.0f;
  float filterSustain = 0.0f;
  float filterReleaseMs = 200.0f;
  float ampAttackMs = 0.0f;
  float ampSustain = 0.7f;
  float ampReleaseMs = 200.0f;
  float gateLenMs = 120.0f;  // gate length for auto release in ADSR
  // Filter algorithm/type/keytracking
  float filterAlgo = 0.0f;   // 0=legacy, 1=SVF
  float filterType = 0.0f;   // 0=LP, 1=BP, 2=HP
  float keytrack = 0.0f;     // 0..1
};

class Tb303ExtSynth {
public:
  explicit Tb303ExtSynth(const Tb303ExtParams& p, double sr) : params_(p), sampleRate_(sr) {}
  void setSampleRate(double sr) { sampleRate_ = sr; }
  void reset() {
    phase_ = 0.0; gate_ = false; envF_ = 0.001f; envA_ = 0.001f; curHz_ = noteToHz(params_.noteSemitones + params_.tuneSemitones);
    fStage_ = Stage::Idle; aStage_ = Stage::Idle; stageSamples_ = 0.0f;
  }
  void noteOn(float noteSemis, float velocity, float accent) {
    params_.noteSemitones = noteSemis; params_.velocity = velocity; params_.accent = accent; gate_ = true;
    // Re-trigger envelopes
    if (params_.envMode > 0.5f) {
      fStage_ = Stage::Attack; aStage_ = Stage::Attack; envF_ = 0.0f; envA_ = 0.0f; stageSamples_ = 0.0f;
    } else {
      // Reset envelope state for legacy decay-only mode to ensure clean retriggers
      envF_ = 1.0f * std::fmin(1.5f, 1.0f + 0.8f * (velocity + accent));
      envA_ = 1.0f * std::fmin(1.5f, 1.0f + 0.5f * (velocity + 0.5f * accent));
    }
    targetHz_ = noteToHz(params_.noteSemitones + params_.tuneSemitones);
  }
  void noteOff() { gate_ = false; }

  float process() {
    // Glide current frequency toward target
    const float glideSamples = static_cast<float>((params_.glideMs * 0.001) * sampleRate_ + 0.5);
    if (glideSamples > 1.0f) {
      const float alpha = 1.0f / glideSamples;
      curHz_ += alpha * (targetHz_ - curHz_);
    } else {
      curHz_ = targetHz_;
    }

    // Envelopes
    if (params_.envMode > 0.5f) {
      advanceAdsr(envF_, fStage_, params_.filterAttackMs, params_.filterDecayMs, params_.filterSustain, params_.filterReleaseMs);
      advanceAdsr(envA_, aStage_, params_.ampAttackMs, params_.ampDecayMs, params_.ampSustain, params_.ampReleaseMs);
    } else {
      const float fTau = std::exp(-1.0f / static_cast<float>((params_.filterDecayMs * 0.001) * sampleRate_ + 1.0));
      const float aTau = std::exp(-1.0f / static_cast<float>((params_.ampDecayMs * 0.001) * sampleRate_ + 1.0));
      envF_ *= fTau;
      envA_ *= aTau;
    }

    // Oscillator
    const double inc = (2.0 * M_PI) * static_cast<double>(curHz_) / sampleRate_;
    phase_ += inc;
    // keep phase wrapped each sample for stable saw computation
    const double twopi = 2.0 * M_PI;
    const double phaseWrapped = std::fmod(phase_, twopi);
    if (phase_ > 1e6) {
      // avoid growth of phase_ over long sessions
      phase_ = phaseWrapped;
    }
    float osc;
    if (params_.waveform == 1) {
      osc = (std::sin(phaseWrapped) >= 0.0 ? 1.0f : -1.0f);
    } else {
      const float ph01 = static_cast<float>(phaseWrapped / twopi);
      osc = 2.0f * ph01 - 1.0f;
    }

    // 3-pole resonant LPF (simple cascaded one-pole with feedback)
    const float envPush = params_.envMod * envF_;
    float cutoff = params_.cutoffHz * (1.0f + envPush);
    if (cutoff < 20.0f) cutoff = 20.0f; if (cutoff > 18000.0f) cutoff = 18000.0f;
    // Keytracking
    const float noteHz = noteToHz(params_.noteSemitones);
    cutoff *= (1.0f + params_.keytrack * ((noteHz / 440.0f) - 1.0f));
    const float a = std::exp(-2.0f * static_cast<float>(M_PI) * cutoff / static_cast<float>(sampleRate_));
    const float b = 1.0f - a;
    const float res = params_.resonance;
    // Pre-filter soft drive (tanh) normalized to preserve level
    float driveAmt = params_.drive;
    if (driveAmt < 0.0f) driveAmt = 0.0f; if (driveAmt > 1.0f) driveAmt = 1.0f;
    const float driveGain = 1.0f + 4.0f * driveAmt;
    const float norm = std::tanh(driveGain);
    const float oscDriven = (norm > 0.0f) ? (std::tanh(osc * driveGain) / norm) : osc;
    float in = oscDriven;
    if (params_.filterAlgo < 0.5f) {
      // Legacy 3-pole with feedback
      in = oscDriven - res * y3_;
      y1_ = a * y1_ + b * in;
      y2_ = a * y2_ + b * y1_;
      y3_ = a * y3_ + b * y2_;
    } else {
      // Simple SVF (Chamberlin) for LP/BP/HP
      const float g = std::tan(static_cast<float>(M_PI) * cutoff / static_cast<float>(sampleRate_));
      const float R = 1.0f - res; // crude mapping
      // Integrators
      float hp = (in - (R * bp_) - lp_) / (1.0f + g);
      bp_ += g * hp;
      lp_ += g * bp_;
      if (params_.filterType < 0.5f) y3_ = lp_; else if (params_.filterType < 1.5f) y3_ = bp_; else y3_ = hp;
    }

    // Output gain; scale by amp ADSR only when ADSR mode is enabled
    const float gainBase = params_.ampGain * (0.6f + 0.4f * params_.velocity) * (1.0f + 0.5f * params_.accent);
    const float ampEnvScale = (params_.envMode > 0.5f) ? envA_ : 1.0f;
    const float sOut = y3_ * gainBase * ampEnvScale;
    return sOut;
  }

  Tb303ExtParams& params() { return params_; }
  const Tb303ExtParams& params() const { return params_; }

private:
  enum class Stage : uint8_t { Idle = 0, Attack, Decay, Sustain, Release };
  void advanceAdsr(float& env, Stage& st, float attMs, float decMs, float sus, float relMs) {
    const float sr = static_cast<float>(sampleRate_);
    const float gateSamps = static_cast<float>(std::max(1.0, (params_.gateLenMs * 0.001) * sr));
    // Auto release after gate length
    if (st != Stage::Idle && stageSamples_ >= gateSamps && st != Stage::Release) {
      st = Stage::Release; stageSamples_ = 0.0f;
    }
    switch (st) {
      case Stage::Idle: env = 0.0f; break;
      case Stage::Attack: {
        const float aS = std::max(1.0f, attMs * 0.001f * sr);
        env += (1.0f - env) * (1.0f / aS);
        if (++stageSamples_ >= aS) { st = Stage::Decay; stageSamples_ = 0.0f; }
        break;
      }
      case Stage::Decay: {
        const float dS = std::max(1.0f, decMs * 0.001f * sr);
        env += (sus - env) * (1.0f / dS);
        if (++stageSamples_ >= dS) { st = Stage::Sustain; stageSamples_ = 0.0f; }
        break;
      }
      case Stage::Sustain: env = sus; stageSamples_ += 1.0f; break;
      case Stage::Release: {
        const float rS = std::max(1.0f, relMs * 0.001f * sr);
        env += (0.0f - env) * (1.0f / rS);
        if (++stageSamples_ >= rS) { st = Stage::Idle; stageSamples_ = 0.0f; env = 0.0f; }
        break;
      }
    }
  }
  static float noteToHz(float semis) { return 440.0f * std::pow(2.0f, (semis - 69.0f) / 12.0f); }

  Tb303ExtParams params_{};
  double sampleRate_ = 48000.0;
  double phase_ = 0.0;
  float envF_ = 0.0f;
  float envA_ = 0.0f;
  float y1_ = 0.0f, y2_ = 0.0f, y3_ = 0.0f;
  bool gate_ = false;
  float curHz_ = 110.0f;
  float targetHz_ = 110.0f;
  Stage fStage_ = Stage::Idle;
  Stage aStage_ = Stage::Idle;
  float stageSamples_ = 0.0f;
  // SVF integrators
  float lp_ = 0.0f;
  float bp_ = 0.0f;
  // future: pan per-voice if stereo processing is added
};


