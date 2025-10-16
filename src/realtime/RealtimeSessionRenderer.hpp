#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <string>
#include <cstdio>
#include <chrono>
#include "../core/Graph.hpp"
#include "../core/ScopedAudioUnit.hpp"
#include "../core/OsStatusUtils.hpp"
#include "../core/Command.hpp"
#include "../session/SessionSpec.hpp"
#include "../core/SpectralDuckerNode.hpp"
#include <cmath>

class RealtimeSessionRenderer {
public:
  RealtimeSessionRenderer() = default;
  ~RealtimeSessionRenderer() { stop(); }

  struct Rack { Graph* graph=nullptr; std::string id; float gain=1.0f; bool muted=false; bool solo=false; };
  template <size_t N>
  void setCommandQueue(SpscCommandQueue<N>* q) { cmdQueue_ = reinterpret_cast<void*>(q); queueDrain_ = [](void* p, SampleTime cutoff, std::vector<Command>& out){ static_cast<SpscCommandQueue<N>*>(p)->drainUpTo(cutoff, out); }; }
  void setDiagnostics(bool printTriggers) { printTriggers_ = printTriggers; }
  void setMeters(bool enabled, double intervalSec) { metersEnabled_ = enabled; metersIntervalSec_ = (intervalSec > 0.05 ? intervalSec : 1.0); }
  void setMetricsNdjson(const char* path, bool includeRacks, bool includeBuses) {
    metricsIncludeRacks_ = includeRacks; metricsIncludeBuses_ = includeBuses;
    if (metricsFile_) { std::fclose(metricsFile_); metricsFile_ = nullptr; }
    if (path && *path) {
      metricsFile_ = std::fopen(path, "w");
      metricsEnabled_ = (metricsFile_ != nullptr);
      if (metricsEnabled_) startWallUnix_ = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    } else {
      metricsEnabled_ = false;
    }
  }
  // Session crossfaders: configure pairs and behavior
  void setXfaders(const std::vector<SessionSpec::XfaderRef>& xs, const std::vector<Rack>& racks) {
    auto indexOf = [&](const std::string& id) -> size_t {
      for (size_t i = 0; i < racks.size(); ++i) if (racks[i].id == id) return i; return SIZE_MAX;
    };
    xfaders_.clear();
    for (const auto& xr : xs) {
      if (xr.racks.size() < 2) continue;
      XfaderState st{}; st.id = xr.id;
      st.aIndex = indexOf(xr.racks[0]);
      st.bIndex = indexOf(xr.racks[1]);
      if (st.aIndex == SIZE_MAX || st.bIndex == SIZE_MAX) continue;
      st.lawEqualPower = (xr.law != std::string("linear"));
      st.smoothingMs = xr.smoothingMs;
      st.lfoEnabled = xr.lfo.has;
      st.freqHz = xr.lfo.freqHz;
      st.phase01 = xr.lfo.phase01;
      if (st.lfoEnabled) {
        const double s = std::sin(2.0 * M_PI * st.phase01);
        st.x = 0.5 * (s + 1.0);
        st.xTarget = st.x;
      } else { st.x = 0.5; st.xTarget = st.x; }
      st.lastGA = st.lastGB = 1.0;
      xfaders_.push_back(st);
    }
  }

  void start(const std::vector<Rack>& racks, const std::vector<SessionSpec::BusRef>& buses, const std::vector<SessionSpec::RouteRef>& routes, double requestedSampleRate, uint32_t channels) {
    if (channels == 0) throw std::invalid_argument("channels must be > 0");
    racks_ = racks; buses_ = buses; routes_ = routes; channels_ = channels;

    AudioComponentDescription desc{}; desc.componentType = kAudioUnitType_Output; desc.componentSubType = kAudioUnitSubType_DefaultOutput; desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) throw std::runtime_error("Default output component not found");

    OSStatus err = AudioComponentInstanceNew(comp, unit_.ptr());
    if (err != noErr) throw std::runtime_error(std::string("AudioComponentInstanceNew failed: ") + osstatusToString(err));

    AudioStreamBasicDescription asbd{}; asbd.mSampleRate = requestedSampleRate; asbd.mFormatID = kAudioFormatLinearPCM; asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked; asbd.mBitsPerChannel = 32; asbd.mChannelsPerFrame = channels_; asbd.mFramesPerPacket = 1; asbd.mBytesPerFrame = 4 * channels_; asbd.mBytesPerPacket = asbd.mBytesPerFrame;
    err = AudioUnitSetProperty(unit_.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    if (err != noErr) throw std::runtime_error(std::string("AudioUnitSetProperty(StreamFormat) failed: ") + osstatusToString(err));

    AURenderCallbackStruct cb{}; cb.inputProc = &RealtimeSessionRenderer::render; cb.inputProcRefCon = this;
    err = AudioUnitSetProperty(unit_.get(), kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0, &cb, sizeof(cb));
    if (err != noErr) throw std::runtime_error(std::string("AudioUnitSetProperty(SetRenderCallback) failed: ") + osstatusToString(err));

    err = AudioUnitInitialize(unit_.get());
    if (err != noErr) throw std::runtime_error(std::string("AudioUnitInitialize failed: ") + osstatusToString(err));

    // Update graphs with actual sample rate
    UInt32 size = sizeof(asbd);
    err = AudioUnitGetProperty(unit_.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, &size);
    sampleRate_ = (err == noErr && asbd.mSampleRate > 0.0) ? asbd.mSampleRate : requestedSampleRate;
    for (const auto& r : racks_) { if (r.graph) { r.graph->prepare(sampleRate_, 1024); r.graph->reset(); } }
    // Prepare meters accumulators
    rackMeters_.assign(racks_.size(), Meter{});
    busMeters_.assign(buses_.size(), Meter{});
    lastMetersPrintSec_ = 0.0;

    err = AudioOutputUnitStart(unit_.get());
    if (err != noErr) throw std::runtime_error(std::string("AudioOutputUnitStart failed: ") + osstatusToString(err));
    sampleCounter_.store(0, std::memory_order_relaxed);
  }

  void stop() { if (metricsFile_) { std::fclose(metricsFile_); metricsFile_ = nullptr; } unit_.release(); }
  double sampleRate() const noexcept { return sampleRate_; }
  SampleTime sampleCounter() const noexcept { return sampleCounter_.load(std::memory_order_relaxed); }
  void resetSampleCounter() { sampleCounter_.store(0, std::memory_order_relaxed); }

private:
  static OSStatus render(void* inRefCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 inNumberFrames, AudioBufferList* ioData) noexcept {
    auto* self = static_cast<RealtimeSessionRenderer*>(inRefCon);
    float* interleaved = static_cast<float*>(ioData->mBuffers[0].mData);
    const SampleTime blockStartAbs = self->sampleCounter_.load(std::memory_order_relaxed);
    const SampleTime cutoff = blockStartAbs + static_cast<SampleTime>(inNumberFrames);

    // Drain commands up to cutoff
    std::vector<Command> drained; drained.clear();
    if (self->cmdQueue_) self->queueDrain_(self->cmdQueue_, cutoff, drained);

    // Determine split offsets
    std::vector<uint32_t> splits; splits.reserve(8); splits.push_back(0); splits.push_back(inNumberFrames);
    for (const auto& c : drained) {
      if (c.sampleTime >= blockStartAbs && c.sampleTime < cutoff) splits.push_back(static_cast<uint32_t>(c.sampleTime - blockStartAbs));
    }
    std::sort(splits.begin(), splits.end()); splits.erase(std::unique(splits.begin(), splits.end()), splits.end());

    // Scratch buffers per rack and per bus
    const size_t segMaxSamples = static_cast<size_t>(inNumberFrames) * self->channels_;
    self->rackScratch_.assign(self->racks_.size(), std::vector<float>(segMaxSamples, 0.0f));
    self->busScratch_.assign(self->buses_.size(), std::vector<float>(segMaxSamples, 0.0f));

    for (size_t si = 0; si + 1 < splits.size(); ++si) {
      const uint32_t segStart = splits[si]; const uint32_t segEnd = splits[si + 1]; const uint32_t segFrames = segEnd - segStart; if (segFrames == 0) continue;
      const SampleTime segAbsStart = blockStartAbs + static_cast<SampleTime>(segStart);
      // Handle xfader param events (nodeId "xfader:<id>:x") at boundary before node events
      for (const auto& ev : drained) if (ev.sampleTime == segAbsStart && (ev.type == CommandType::SetParam || ev.type == CommandType::SetParamRamp)) {
        if (ev.nodeId) {
          const char* nid = ev.nodeId;
          if (std::strncmp(nid, "xfader:", 8) == 0) {
            const char* rest = nid + 8; // <id>:x
            const char* colon = std::strchr(rest, ':');
            if (colon && std::strcmp(colon + 1, "x") == 0) {
              const std::string xfId(rest, static_cast<size_t>(colon - rest));
              for (auto& xf : self->xfaders_) {
                if (xf.id == xfId) {
                  xf.lfoEnabled = false; // manual takes over
                  double v = static_cast<double>(ev.value);
                  if (v < 0.0) v = 0.0; else if (v > 1.0) v = 1.0;
                  xf.xTarget = v;
                  if (ev.type == CommandType::SetParamRamp && ev.rampMs > 0.0f) xf.smoothingMs = static_cast<double>(ev.rampMs);
                }
              }
            }
          }
        }
      }
      // Apply events at segment boundary: SetParam first, then Trigger across all racks
      for (const auto& ev : drained) if (ev.sampleTime == segAbsStart && (ev.type == CommandType::SetParam || ev.type == CommandType::SetParamRamp)) {
        // Route events by matching full, prefixed nodeId against graph node ids
        for (const auto& r : self->racks_) if (r.graph) {
          if (!ev.nodeId) continue;
          const char* fullId = ev.nodeId;
          r.graph->forEachNode([&](const std::string& id, Node& n){ if (id == fullId) n.handleEvent(ev); });
        }
        if (self->printTriggers_) self->printEvent(ev, segAbsStart);
      }
      for (const auto& ev : drained) if (ev.sampleTime == segAbsStart && ev.type == CommandType::Trigger) {
        // Route events by matching full, prefixed nodeId against graph node ids
        for (const auto& r : self->racks_) if (r.graph) {
          if (!ev.nodeId) continue;
          const char* fullId = ev.nodeId;
          r.graph->forEachNode([&](const std::string& id, Node& n){ if (id == fullId) n.handleEvent(ev); });
        }
        if (self->printTriggers_) self->printEvent(ev, segAbsStart);
      }

      // Zero output segment
      float* outPtr = interleaved + static_cast<size_t>(segStart) * self->channels_;
      std::memset(outPtr, 0, static_cast<size_t>(segFrames) * self->channels_ * sizeof(float));

      // Determine solo mode for this segment
      bool anySolo = false; for (const auto& r : self->racks_) if (r.solo) { anySolo = true; break; }
      // Render each rack graph to its scratch
      for (size_t ri = 0; ri < self->racks_.size(); ++ri) {
        const auto& rackCfg = self->racks_[ri];
        if (rackCfg.muted) continue;
        if (anySolo && !rackCfg.solo) continue;
        auto* g = rackCfg.graph; if (!g) continue;
        ProcessContext ctx{}; ctx.sampleRate = self->sampleRate_; ctx.frames = segFrames; ctx.blockStart = segAbsStart;
        float* dst = self->rackScratch_[ri].data();
        for (size_t i = 0; i < static_cast<size_t>(segFrames) * self->channels_; ++i) dst[i] = 0.0f;
        g->process(ctx, dst, self->channels_);
        // Update meters accumulators
        if (self->metersEnabled_) {
          Meter& m = self->rackMeters_[ri];
          const size_t n = static_cast<size_t>(segFrames) * self->channels_;
          for (size_t i = 0; i < n; ++i) {
            const float s = dst[i];
            const float a = std::fabs(s);
            if (a > m.peak) m.peak = a;
            m.sumSq += static_cast<double>(s) * static_cast<double>(s);
          }
          m.frames += segFrames * self->channels_;
        }
      }
      // Zero bus scratch
      for (auto& bbuf : self->busScratch_) {
        const size_t count = static_cast<size_t>(segFrames) * self->channels_;
        for (size_t i = 0; i < count; ++i) bbuf[i] = 0.0f;
      }
      // Compute per-rack gain multipliers from xfaders (apply once per segment)
      std::vector<float> rackGainMul(self->racks_.size(), 1.0f);
      for (auto& xf : self->xfaders_) {
        if (xf.lfoEnabled) {
          const double dt = static_cast<double>(segFrames) / self->sampleRate_;
          xf.phase01 += xf.freqHz * dt; while (xf.phase01 >= 1.0) xf.phase01 -= 1.0;
          const double s = std::sin(2.0 * M_PI * xf.phase01);
          xf.xTarget = 0.5 * (s + 1.0);
        }
        const double alpha = std::min(1.0, (xf.smoothingMs > 0.0 ? (static_cast<double>(segFrames) / self->sampleRate_) / (xf.smoothingMs / 1000.0) : 1.0));
        xf.x += (xf.xTarget - xf.x) * alpha;
        double gA = 1.0, gB = 1.0;
        const double x = std::clamp(xf.x, 0.0, 1.0);
        if (xf.lawEqualPower) { gA = std::cos(0.5 * M_PI * x); gB = std::sin(0.5 * M_PI * x); }
        else { gA = 1.0 - x; gB = x; }
        xf.lastGA = gA; xf.lastGB = gB;
        if (xf.aIndex < rackGainMul.size()) rackGainMul[xf.aIndex] *= static_cast<float>(gA);
        if (xf.bIndex < rackGainMul.size()) rackGainMul[xf.bIndex] *= static_cast<float>(gB);
      }
      // Route racks to buses
      std::vector<bool> rackHadRoute(self->racks_.size(), false);
      for (const auto& rt : self->routes_) {
        size_t ri = 0; bool foundRack = false; for (; ri < self->racks_.size(); ++ri) if (self->racks_[ri].id == rt.from) { foundRack = true; break; }
        if (!foundRack) continue;
        if (self->racks_[ri].muted) continue; // muted racks do not route
        // honor solo globally: if anySolo and this rack not solo, skip
        bool anySolo2 = false; for (const auto& r : self->racks_) if (r.solo) { anySolo2 = true; break; }
        if (anySolo2 && !self->racks_[ri].solo) continue;
        size_t bi = 0; bool foundBus = false; for (; bi < self->buses_.size(); ++bi) if (self->buses_[bi].id == rt.to) { foundBus = true; break; }
        if (!foundBus) continue;
        const float gain = self->racks_[ri].gain * rt.gain * ((ri < rackGainMul.size()) ? rackGainMul[ri] : 1.0f);
        const float* src = self->rackScratch_[ri].data(); float* dst = self->busScratch_[bi].data();
        const size_t n = static_cast<size_t>(segFrames) * self->channels_;
        for (size_t i = 0; i < n; ++i) dst[i] += src[i] * gain;
        rackHadRoute[ri] = true;
      }
      // Fallback: racks without routes → sum to output
      for (size_t ri = 0; ri < self->racks_.size(); ++ri) {
        if (ri >= rackHadRoute.size() || rackHadRoute[ri]) continue;
        if (self->racks_[ri].muted) continue;
        bool anySolo3 = false; for (const auto& r : self->racks_) if (r.solo) { anySolo3 = true; break; }
        if (anySolo3 && !self->racks_[ri].solo) continue;
        const float* src = self->rackScratch_[ri].data();
        float* dst = outPtr;
        const size_t n = static_cast<size_t>(segFrames) * self->channels_;
        const float mul = ((ri < rackGainMul.size()) ? rackGainMul[ri] : 1.0f) * self->racks_[ri].gain;
        for (size_t i = 0; i < n; ++i) dst[i] += src[i] * mul;
      }
      // Apply inserts (spectral_ducker) on each bus
      for (size_t bi = 0; bi < self->buses_.size(); ++bi) {
        auto& bus = self->buses_[bi];
        for (const auto& ins : bus.inserts) {
          if (ins.type == std::string("spectral_ducker")) {
            SpectralDuckerNode duck; try {
              if (ins.params.contains("mix")) duck.mix = ins.params["mix"].get<float>();
              if (ins.params.contains("detectorHpfHz")) duck.scHpfHz = ins.params["detectorHpfHz"].get<float>();
              if (ins.params.contains("applyMode")) { std::string m = ins.params["applyMode"].get<std::string>(); duck.applyMode = (m == std::string("dynamicEq")) ? SpectralDuckerNode::ApplyMode::DynamicEq : SpectralDuckerNode::ApplyMode::Multiply; }
              if (ins.params.contains("stereoMode")) { std::string sm = ins.params["stereoMode"].get<std::string>(); duck.stereoMode = (sm == std::string("MidSide")) ? SpectralDuckerNode::StereoMode::MidSide : SpectralDuckerNode::StereoMode::LR; }
              if (ins.params.contains("msSideScale")) duck.msSideScale = ins.params["msSideScale"].get<float>();
            } catch (...) {}
            duck.prepare(self->sampleRate_, std::min<uint32_t>(segFrames, 4096u));
            std::vector<float> sc; sc.assign(static_cast<size_t>(segFrames) * self->channels_, 0.0f);
            for (const auto& scp : ins.sidechains) {
              const std::string& fromRack = scp.second; size_t ri = 0; bool found = false; for (; ri < self->racks_.size(); ++ri) if (self->racks_[ri].id == fromRack) { found = true; break; }
              if (!found) continue; const float* s = self->rackScratch_[ri].data();
              const size_t n = static_cast<size_t>(segFrames) * self->channels_;
              for (size_t i = 0; i < n; ++i) sc[i] += s[i];
            }
            ProcessContext bctx{}; bctx.sampleRate = self->sampleRate_; bctx.frames = segFrames; bctx.blockStart = segAbsStart;
            duck.applySidechain(bctx, self->busScratch_[bi].data(), sc.data(), self->channels_);
          }
        }
      }
      // Accumulate bus meters after inserts
      if (self->metersEnabled_) {
        for (size_t bi = 0; bi < self->busScratch_.size(); ++bi) {
          Meter& m = self->busMeters_[bi];
          const size_t n = static_cast<size_t>(segFrames) * self->channels_;
          const float* src = self->busScratch_[bi].data();
          for (size_t i = 0; i < n; ++i) {
            const float s = src[i];
            const float a = std::fabs(s);
            if (a > m.peak) m.peak = a;
            m.sumSq += static_cast<double>(s) * static_cast<double>(s);
          }
          m.frames += segFrames * self->channels_;
        }
      }
      // Sum buses to output
      for (size_t bi = 0; bi < self->busScratch_.size(); ++bi) {
        const size_t n = static_cast<size_t>(segFrames) * self->channels_;
        const float* src = self->busScratch_[bi].data();
        for (size_t i = 0; i < n; ++i) outPtr[i] += src[i];
      }
      // Periodic meters + metrics
      if (self->metersEnabled_) {
        const double nowSec = static_cast<double>(cutoff) / self->sampleRate_;
        if (nowSec - self->lastMetersPrintSec_ >= self->metersIntervalSec_) {
          const double tRel = nowSec;
          const double tsUnix = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
          for (size_t ri = 0; ri < self->racks_.size(); ++ri) {
            const Meter& m = self->rackMeters_[ri]; if (m.frames == 0) continue;
            const double peakDb = (m.peak > 0.0) ? (20.0 * std::log10(static_cast<double>(m.peak))) : -std::numeric_limits<double>::infinity();
            const double rmsLin = std::sqrt(m.sumSq / static_cast<double>(m.frames));
            const double rmsDb = (rmsLin > 0.0) ? (20.0 * std::log10(rmsLin)) : -std::numeric_limits<double>::infinity();
            std::fprintf(stderr, "Meters\track=%s\tpeak_dBFS=%.2f\trms_dBFS=%.2f\n", self->racks_[ri].id.c_str(), peakDb, rmsDb);
            if (self->metricsEnabled_ && self->metricsFile_ && self->metricsIncludeRacks_) {
              std::fprintf(self->metricsFile_,
                           "{\"event\":\"meters\",\"ts_unix\":%.6f,\"t_rel\":%.6f,\"interval_s\":%.3f,\"sr\":%.0f,\"channels\":%u,\"kind\":\"rack\",\"id\":\"%s\",\"peak_dbfs\":%.3f,\"rms_dbfs\":%.3f}\n",
                           tsUnix, tRel, self->metersIntervalSec_, self->sampleRate_, self->channels_, self->racks_[ri].id.c_str(), peakDb, rmsDb);
            }
          }
          for (size_t bi = 0; bi < self->buses_.size(); ++bi) {
            const Meter& m = self->busMeters_[bi]; if (m.frames == 0) continue;
            const double peakDb = (m.peak > 0.0) ? (20.0 * std::log10(static_cast<double>(m.peak))) : -std::numeric_limits<double>::infinity();
            const double rmsLin = std::sqrt(m.sumSq / static_cast<double>(m.frames));
            const double rmsDb = (rmsLin > 0.0) ? (20.0 * std::log10(rmsLin)) : -std::numeric_limits<double>::infinity();
            std::fprintf(stderr, "Meters\tbus=%s\tpeak_dBFS=%.2f\trms_dBFS=%.2f\n", self->buses_[bi].id.c_str(), peakDb, rmsDb);
            if (self->metricsEnabled_ && self->metricsFile_ && self->metricsIncludeBuses_) {
              std::fprintf(self->metricsFile_,
                           "{\"event\":\"meters\",\"ts_unix\":%.6f,\"t_rel\":%.6f,\"interval_s\":%.3f,\"sr\":%.0f,\"channels\":%u,\"kind\":\"bus\",\"id\":\"%s\",\"peak_dbfs\":%.3f,\"rms_dbfs\":%.3f}\n",
                           tsUnix, tRel, self->metersIntervalSec_, self->sampleRate_, self->channels_, self->buses_[bi].id.c_str(), peakDb, rmsDb);
            }
          }
          if (self->metricsEnabled_ && self->metricsFile_) {
            for (const auto& xf : self->xfaders_) {
              std::fprintf(self->metricsFile_,
                           "{\"event\":\"xfader\",\"ts_unix\":%.6f,\"t_rel\":%.6f,\"id\":\"%s\",\"x\":%.4f,\"gainA\":%.4f,\"gainB\":%.4f}\n",
                           tsUnix, tRel, xf.id.c_str(), xf.x, xf.lastGA, xf.lastGB);
            }
            std::fflush(self->metricsFile_);
          }
          for (auto& m : self->rackMeters_) { m = Meter{}; }
          for (auto& m : self->busMeters_) { m = Meter{}; }
          self->lastMetersPrintSec_ = nowSec;
        }
      }
    }

    self->sampleCounter_.store(cutoff, std::memory_order_relaxed);
    return noErr;
  }

  void printEvent(const Command& c, SampleTime segAbsStart) const {
    const double tSec = static_cast<double>(segAbsStart) / sampleRate_;
    const char* tag = (c.type == CommandType::Trigger) ? "TRIGGER" : (c.type == CommandType::SetParam ? "SET" : "RAMP");
    std::fprintf(stderr, "%.6f\t%s\tnode=%s\tpid=%u\tval=%.3f\n", tSec, tag, c.nodeId ? c.nodeId : "", c.paramId, static_cast<double>(c.value));
  }

  AudioUnitHandle unit_{};
  std::vector<Rack> racks_{};
  std::vector<SessionSpec::BusRef> buses_{};
  std::vector<SessionSpec::RouteRef> routes_{};
  uint32_t channels_ = 2;
  double sampleRate_ = 48000.0;
  std::atomic<SampleTime> sampleCounter_{0};
  void* cmdQueue_ = nullptr; using DrainFn = void(*)(void*, SampleTime, std::vector<Command>&); DrainFn queueDrain_ = nullptr;
  bool printTriggers_ = false;
  std::vector<std::vector<float>> rackScratch_{};
  std::vector<std::vector<float>> busScratch_{};
  struct Meter { double sumSq = 0.0; double peak = 0.0; uint64_t frames = 0; };
  std::vector<Meter> rackMeters_{};
  std::vector<Meter> busMeters_{};
  bool metersEnabled_ = false; double metersIntervalSec_ = 1.0; double lastMetersPrintSec_ = 0.0;
  bool metricsEnabled_ = false; FILE* metricsFile_ = nullptr; bool metricsIncludeRacks_ = true; bool metricsIncludeBuses_ = true; double startWallUnix_ = 0.0;
  struct XfaderState { std::string id; size_t aIndex=SIZE_MAX; size_t bIndex=SIZE_MAX; bool lawEqualPower=true; double smoothingMs=10.0; bool lfoEnabled=false; double freqHz=0.25; double phase01=0.0; double x=0.0; double xTarget=0.0; double lastGA=1.0; double lastGB=1.0; };
  std::vector<XfaderState> xfaders_{};
};


