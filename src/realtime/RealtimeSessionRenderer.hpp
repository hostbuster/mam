#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstring>
#include "../core/Graph.hpp"
#include "../core/ScopedAudioUnit.hpp"
#include "../core/OsStatusUtils.hpp"
#include "../core/Command.hpp"
#include "../session/SessionSpec.hpp"
#include "../core/SpectralDuckerNode.hpp"

class RealtimeSessionRenderer {
public:
  RealtimeSessionRenderer() = default;
  ~RealtimeSessionRenderer() { stop(); }

  struct Rack { Graph* graph=nullptr; std::string id; float gain=1.0f; };
  template <size_t N>
  void setCommandQueue(SpscCommandQueue<N>* q) { cmdQueue_ = reinterpret_cast<void*>(q); queueDrain_ = [](void* p, SampleTime cutoff, std::vector<Command>& out){ static_cast<SpscCommandQueue<N>*>(p)->drainUpTo(cutoff, out); }; }
  void setDiagnostics(bool printTriggers) { printTriggers_ = printTriggers; }

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

    err = AudioOutputUnitStart(unit_.get());
    if (err != noErr) throw std::runtime_error(std::string("AudioOutputUnitStart failed: ") + osstatusToString(err));
    sampleCounter_.store(0, std::memory_order_relaxed);
  }

  void stop() { unit_.release(); }
  double sampleRate() const noexcept { return sampleRate_; }
  SampleTime sampleCounter() const noexcept { return sampleCounter_.load(std::memory_order_relaxed); }

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
      // Apply events at segment boundary: SetParam first, then Trigger across all racks
      for (const auto& ev : drained) if (ev.sampleTime == segAbsStart && (ev.type == CommandType::SetParam || ev.type == CommandType::SetParamRamp)) {
        for (const auto& r : self->racks_) if (r.graph) r.graph->forEachNode([&](const std::string& id, Node& n){ if (ev.nodeId && id == ev.nodeId) n.handleEvent(ev); });
        if (self->printTriggers_) self->printEvent(ev, segAbsStart);
      }
      for (const auto& ev : drained) if (ev.sampleTime == segAbsStart && ev.type == CommandType::Trigger) {
        for (const auto& r : self->racks_) if (r.graph) r.graph->forEachNode([&](const std::string& id, Node& n){ if (ev.nodeId && id == ev.nodeId) n.handleEvent(ev); });
        if (self->printTriggers_) self->printEvent(ev, segAbsStart);
      }

      // Zero output segment
      float* outPtr = interleaved + static_cast<size_t>(segStart) * self->channels_;
      std::memset(outPtr, 0, static_cast<size_t>(segFrames) * self->channels_ * sizeof(float));

      // Render each rack graph to its scratch
      for (size_t ri = 0; ri < self->racks_.size(); ++ri) {
        auto* g = self->racks_[ri].graph; if (!g) continue;
        ProcessContext ctx{}; ctx.sampleRate = self->sampleRate_; ctx.frames = segFrames; ctx.blockStart = segAbsStart;
        float* dst = self->rackScratch_[ri].data();
        for (size_t i = 0; i < static_cast<size_t>(segFrames) * self->channels_; ++i) dst[i] = 0.0f;
        g->process(ctx, dst, self->channels_);
      }
      // Zero bus scratch
      for (auto& bbuf : self->busScratch_) {
        const size_t count = static_cast<size_t>(segFrames) * self->channels_;
        for (size_t i = 0; i < count; ++i) bbuf[i] = 0.0f;
      }
      // Route racks to buses
      std::vector<bool> rackHadRoute(self->racks_.size(), false);
      for (const auto& rt : self->routes_) {
        // find rack index
        size_t ri = 0; bool foundRack = false; for (; ri < self->racks_.size(); ++ri) if (self->racks_[ri].id == rt.from) { foundRack = true; break; }
        if (!foundRack) continue;
        size_t bi = 0; bool foundBus = false; for (; bi < self->buses_.size(); ++bi) if (self->buses_[bi].id == rt.to) { foundBus = true; break; }
        if (!foundBus) continue;
        const float gain = self->racks_[ri].gain * rt.gain;
        const float* src = self->rackScratch_[ri].data(); float* dst = self->busScratch_[bi].data();
        const size_t n = static_cast<size_t>(segFrames) * self->channels_;
        for (size_t i = 0; i < n; ++i) dst[i] += src[i] * gain;
        rackHadRoute[ri] = true;
      }
      // Fallback: if a rack has no route, sum it directly to output (MVP behavior like offline)
      for (size_t ri = 0; ri < self->racks_.size(); ++ri) {
        if (ri >= rackHadRoute.size() || rackHadRoute[ri]) continue;
        const float* src = self->rackScratch_[ri].data();
        float* dst = outPtr;
        const size_t n = static_cast<size_t>(segFrames) * self->channels_;
        for (size_t i = 0; i < n; ++i) dst[i] += src[i] * self->racks_[ri].gain;
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
            // Build sidechain by summing referenced rack scratch
            std::vector<float> sc; sc.assign(static_cast<size_t>(segFrames) * self->channels_, 0.0f);
            for (const auto& scp : ins.sidechains) {
              const std::string& fromRack = scp.second; size_t ri = 0; bool found = false; for (; ri < self->racks_.size(); ++ri) if (self->racks_[ri].id == fromRack) { found = true; break; }
              if (!found) continue; const float* src = self->rackScratch_[ri].data();
              const size_t n = static_cast<size_t>(segFrames) * self->channels_;
              for (size_t i = 0; i < n; ++i) sc[i] += src[i];
            }
            ProcessContext bctx{}; bctx.sampleRate = self->sampleRate_; bctx.frames = segFrames; bctx.blockStart = segAbsStart;
            duck.applySidechain(bctx, self->busScratch_[bi].data(), sc.data(), self->channels_);
          }
        }
      }
      // Sum buses to output
      for (size_t bi = 0; bi < self->busScratch_.size(); ++bi) {
        const size_t n = static_cast<size_t>(segFrames) * self->channels_;
        const float* src = self->busScratch_[bi].data();
        for (size_t i = 0; i < n; ++i) outPtr[i] += src[i];
      }
    }

    self->sampleCounter_.store(cutoff, std::memory_order_relaxed);
    return noErr;
  }

  void printEvent(const Command& c, SampleTime segAbsStart) const {
    const double tSec = static_cast<double>(segAbsStart) / sampleRate_;
    const char* tag = (c.type == CommandType::Trigger) ? "TRIGGER" : (c.type == CommandType::SetParam ? "SET" : "RAMP");
    std::fprintf(stderr, "%s t=%.6fs node=%s pid=%u val=%.3f\n", tag, tSec, c.nodeId ? c.nodeId : "", c.paramId, static_cast<double>(c.value));
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
};


