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

class RealtimeGraphRenderer {
public:
  RealtimeGraphRenderer() = default;
  ~RealtimeGraphRenderer() { stop(); }

  void setCommandQueue(SpscCommandQueue<2048>* q) { cmdQueue_ = q; }

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

private:
  static OSStatus render(void* inRefCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 inNumberFrames, AudioBufferList* ioData) noexcept {
    auto* self = static_cast<RealtimeGraphRenderer*>(inRefCon);
    // Interleaved: single buffer expected
    float* interleaved = static_cast<float*>(ioData->mBuffers[0].mData);

    // Compute split points for sample-accurate event application
    const SampleTime blockStartAbs = self->sampleCounter_;
    const SampleTime cutoff = blockStartAbs + static_cast<SampleTime>(inNumberFrames);
    std::vector<uint32_t> splitOffsets;
    splitOffsets.reserve(8);
    splitOffsets.push_back(0);
    if (self->cmdQueue_) {
      self->drained_.clear();
      self->cmdQueue_->drainUpTo(cutoff, self->drained_);
      for (const Command& c : self->drained_) {
        if (c.sampleTime >= blockStartAbs && c.sampleTime < cutoff) {
          const uint32_t off = static_cast<uint32_t>(c.sampleTime - blockStartAbs);
          splitOffsets.push_back(off);
        }
      }
      self->sampleCounter_ = cutoff;
    } else {
      self->sampleCounter_ = cutoff;
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
        for (const Command& c : self->drained_) {
          if (c.sampleTime == segAbsStart && c.nodeId) {
            self->graph_->forEachNode([&](const std::string& id, Node& n){
              if (id == c.nodeId) n.handleEvent(c);
            });
          }
        }
      }

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
  SampleTime sampleCounter_ = 0;
  SpscCommandQueue<2048>* cmdQueue_ = nullptr;
  std::vector<Command> drained_;
};



