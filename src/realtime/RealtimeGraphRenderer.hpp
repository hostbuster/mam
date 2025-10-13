#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <cstdint>
#include <stdexcept>
#include <string>
#include "../core/Graph.hpp"
#include "../core/ScopedAudioUnit.hpp"
#include "../core/OsStatusUtils.hpp"
#include "../core/Command.hpp"
#include <vector>
#include <algorithm>
#include <atomic>
#include "../core/TransportNode.hpp"

class RealtimeGraphRenderer {
public:
  RealtimeGraphRenderer() = default;
  ~RealtimeGraphRenderer() { stop(); }

  void setCommandQueue(SpscCommandQueue<2048>* q) { cmdQueue_ = q; }
  void setDiagnostics(bool printTriggers, double bpmForBeats, uint32_t resolutionStepsPerBar) {
    printTriggers_ = printTriggers; diagBpm_ = bpmForBeats; diagResolution_ = (resolutionStepsPerBar == 0 ? 16u : resolutionStepsPerBar);
  }
  void setDiagLoop(uint64_t loopFrames) { diagLoopFrames_ = loopFrames; }
  void setTransportEmitEnabled(bool enabled) { transportEmitEnabled_ = enabled; }

  void start(Graph& graph, double requestedSampleRate, uint32_t channels) {
    if (channels == 0) throw std::invalid_argument("channels must be > 0");
    graph_ = &graph;
    channels_ = channels;

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) throw std::runtime_error("Default output component not found");

    OSStatus err = AudioComponentInstanceNew(comp, unit_.ptr());
    if (err != noErr) throw std::runtime_error(std::string("AudioComponentInstanceNew failed: ") + osstatusToString(err));

    // Interleaved Float32 stereo (or N channels)
    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = requestedSampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked; // interleaved
    asbd.mBitsPerChannel = 32;
    asbd.mChannelsPerFrame = channels_;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 4 * channels_;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame;

    err = AudioUnitSetProperty(unit_.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    if (err != noErr) throw std::runtime_error(std::string("AudioUnitSetProperty(StreamFormat) failed: ") + osstatusToString(err));

    AURenderCallbackStruct cb{};
    cb.inputProc = &RealtimeGraphRenderer::render;
    cb.inputProcRefCon = this;
    err = AudioUnitSetProperty(unit_.get(), kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0, &cb, sizeof(cb));
    if (err != noErr) throw std::runtime_error(std::string("AudioUnitSetProperty(SetRenderCallback) failed: ") + osstatusToString(err));

    err = AudioUnitInitialize(unit_.get());
    if (err != noErr) throw std::runtime_error(std::string("AudioUnitInitialize failed: ") + osstatusToString(err));

    // Update graph with actual sample rate
    UInt32 size = sizeof(asbd);
    err = AudioUnitGetProperty(unit_.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, &size);
    if (err == noErr && asbd.mSampleRate > 0.0) {
      graph_->prepare(asbd.mSampleRate, 1024);
      graph_->reset();
      sampleRate_ = asbd.mSampleRate;
    } else {
      graph_->prepare(requestedSampleRate, 1024);
      graph_->reset();
      sampleRate_ = requestedSampleRate;
    }

    err = AudioOutputUnitStart(unit_.get());
    if (err != noErr) throw std::runtime_error(std::string("AudioOutputUnitStart failed: ") + osstatusToString(err));
    sampleCounter_ = 0;
  }

  void stop() {
    unit_.release();
  }

  double sampleRate() const noexcept { return sampleRate_; }
  SampleTime sampleCounter() const noexcept { return sampleCounter_.load(std::memory_order_relaxed); }

private:
  static OSStatus render(void* inRefCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 inNumberFrames, AudioBufferList* ioData) noexcept {
    auto* self = static_cast<RealtimeGraphRenderer*>(inRefCon);
    // Interleaved: single buffer expected
    float* interleaved = static_cast<float*>(ioData->mBuffers[0].mData);

    // Compute split points for sample-accurate event application
    const SampleTime blockStartAbs = self->sampleCounter_.load(std::memory_order_relaxed);
    const SampleTime cutoff = blockStartAbs + static_cast<SampleTime>(inNumberFrames);
    // Print loop boundary if it falls at block start or anywhere within this block,
    // before applying triggers (ensures visibility even when boundary is between triggers)
    if (self->printTriggers_ && self->diagLoopFrames_ > 0) {
      const uint64_t L = self->diagLoopFrames_;
      if (L > 0) {
        const uint64_t start = static_cast<uint64_t>(blockStartAbs);
        const uint64_t end = static_cast<uint64_t>(cutoff);
        if (start > 0 && (start % L) == 0ull) {
          const uint64_t loopIdx = start / L;
          std::fprintf(stderr, "Loop %llu\n", static_cast<unsigned long long>(loopIdx));
        } else if (end > start) {
          const uint64_t next = ((start + L - 1ull) / L) * L; // first multiple in [start, ∞)
          if (next >= start && next < end && next > 0ull) {
            const uint64_t loopIdx = next / L;
            std::fprintf(stderr, "Loop %llu\n", static_cast<unsigned long long>(loopIdx));
          }
        }
      }
    }
    std::vector<uint32_t> splitOffsets;
    splitOffsets.reserve(8);
    splitOffsets.push_back(0);
    if (self->cmdQueue_) {
      self->drained_.clear();
      self->cmdQueue_->drainUpTo(cutoff, self->drained_);
        // Sort events to stabilize segment splits and de-duplicate identical ones on the same sample
        std::sort(self->drained_.begin(), self->drained_.end(), [](const Command& a, const Command& b){
          if (a.sampleTime != b.sampleTime) return a.sampleTime < b.sampleTime;
          if (a.nodeId != b.nodeId) return std::strcmp(a.nodeId ? a.nodeId : "", b.nodeId ? b.nodeId : "") < 0;
          if (a.type != b.type) return a.type < b.type;
          if (a.paramId != b.paramId) return a.paramId < b.paramId;
          return a.value < b.value;
        });
        self->drained_.erase(std::unique(self->drained_.begin(), self->drained_.end(), [](const Command& x, const Command& y){
          return x.sampleTime == y.sampleTime && x.nodeId && y.nodeId && std::strcmp(x.nodeId, y.nodeId) == 0 && x.type == y.type && x.paramId == y.paramId;
        }), self->drained_.end());
      for (const Command& c : self->drained_) {
        if (c.sampleTime >= blockStartAbs && c.sampleTime < cutoff) {
          const uint32_t off = static_cast<uint32_t>(c.sampleTime - blockStartAbs);
          splitOffsets.push_back(off);
        }
      }
      self->sampleCounter_.store(cutoff, std::memory_order_relaxed);
    } else {
      self->sampleCounter_.store(cutoff, std::memory_order_relaxed);
    }
    splitOffsets.push_back(inNumberFrames);
    std::sort(splitOffsets.begin(), splitOffsets.end());
    splitOffsets.erase(std::unique(splitOffsets.begin(), splitOffsets.end()), splitOffsets.end());

    // Render each segment and apply events at boundaries
    for (size_t si = 0; si + 1 < splitOffsets.size(); ++si) {
      const uint32_t segStart = splitOffsets[si];
      const uint32_t segEnd = splitOffsets[si + 1];
      const uint32_t segFrames = segEnd - segStart;
      if (segFrames == 0) continue;

      // Deliver events that occur exactly at this segment start
      if (self->cmdQueue_ && !self->drained_.empty()) {
        const SampleTime segAbsStart = blockStartAbs + static_cast<SampleTime>(segStart);
        // 1) Apply SetParam/SetParamRamp first (latch values before triggers), matching offline
        for (const Command& c : self->drained_) {
          if (c.sampleTime == segAbsStart && c.nodeId && (c.type == CommandType::SetParam || c.type == CommandType::SetParamRamp)) {
            if (self->printTriggers_) self->printEvent(c.type == CommandType::SetParam ? "SET" : "RAMP", c);
            self->graph_->forEachNode([&](const std::string& id, Node& n){ if (id == c.nodeId) n.handleEvent(c); });
          }
        }
        // 2) Then apply Triggers
        for (const Command& c : self->drained_) {
          if (c.sampleTime == segAbsStart && c.nodeId && c.type == CommandType::Trigger) {
            if (self->printTriggers_) self->printEvent("TRIGGER", c);
            self->graph_->forEachNode([&](const std::string& id, Node& n){ if (id == c.nodeId) n.handleEvent(c); });
          }
        }
      }

      // Let transport-like nodes emit events at exact sample offsets across the whole segment
      self->graph_->forEachNode([&](const std::string& id, Node& n){
        (void)id;
        if (self->transportEmitEnabled_) if (auto* t = dynamic_cast<TransportNode*>(&n)) {
          SampleTime cursor = blockStartAbs + static_cast<SampleTime>(segStart);
          const SampleTime segAbsEnd = blockStartAbs + static_cast<SampleTime>(segEnd);
          // Emit all events at or after cursor; if next event is before cursor (wrap), allow it on the boundary to avoid missing step 0
          while (true) {
            const SampleTime next = t->nextEventSample();
            if (next < cursor) break; // already emitted earlier in this block
            if (next >= segAbsEnd) break;
            t->emitIfMatch(next, [&](const Command& c){
              if (self->printTriggers_) self->printEvent("TRANSPORT", c);
              self->graph_->forEachNode([&](const std::string& nid, Node& nn){ if (nid == c.nodeId) nn.handleEvent(c); });
            });
            cursor = next + 1;
          }
        }
      });

      ProcessContext ctx{};
      ctx.sampleRate = self->sampleRate_;
      ctx.frames = segFrames;
      ctx.blockStart = blockStartAbs + static_cast<SampleTime>(segStart);
      float* outPtr = interleaved + static_cast<size_t>(segStart) * self->channels_;
      self->graph_->process(ctx, outPtr, self->channels_);
    }
    return noErr;
  }

  AudioUnitHandle unit_{};
  Graph* graph_ = nullptr;
  uint32_t channels_ = 2;
  double sampleRate_ = 48000.0;
  std::atomic<SampleTime> sampleCounter_{0};
  SpscCommandQueue<2048>* cmdQueue_ = nullptr;
  std::vector<Command> drained_;
  bool printTriggers_ = false;
  double diagBpm_ = 120.0;
  uint32_t diagResolution_ = 16;
  bool transportEmitEnabled_ = false;
  uint64_t diagLoopFrames_ = 0;

  void printEvent(const char* tag, const Command& c) const {
    const uint64_t st = c.sampleTime;
    const uint64_t within = (diagLoopFrames_ > 0) ? (st % diagLoopFrames_) : st;
    // Compute integer bar/step using samples to avoid FP drift
    const double framesPerBarD = (diagBpm_ > 0.0) ? ((60.0 * 4.0) * sampleRate_ / diagBpm_) : (sampleRate_ * 2.0);
    const uint64_t framesPerBar = static_cast<uint64_t>(framesPerBarD + 0.5);
    const uint64_t barIdx = (framesPerBar > 0) ? (within / framesPerBar) : 0ull;
    const uint64_t withinBar = (framesPerBar > 0) ? (within % framesPerBar) : within;
    const uint64_t stepIdx = (framesPerBar > 0 && diagResolution_ > 0)
      ? ((withinBar * static_cast<uint64_t>(diagResolution_) + framesPerBar / 2) / framesPerBar)
      : 0ull;
    const uint64_t stepClamped = (stepIdx >= diagResolution_) ? (diagResolution_ - 1) : stepIdx;
    const double tSec = static_cast<double>(within) / sampleRate_;
    std::fprintf(stderr, "%s t=%.6fs bar=%llu step=%llu node=%s type=%u pid=%u val=%.3f\n",
                 tag, tSec, static_cast<unsigned long long>(barIdx + 1ull), static_cast<unsigned long long>(stepClamped + 1ull),
                 c.nodeId ? c.nodeId : "", static_cast<unsigned>(c.type), c.paramId, static_cast<double>(c.value));
  }
};



