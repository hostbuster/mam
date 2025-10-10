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
    ProcessContext ctx{};
    ctx.sampleRate = self->sampleRate_;
    ctx.frames = inNumberFrames;
    ctx.blockStart = self->sampleCounter_;
    // Future: drain commands to sample-accurate events per node using SpscCommandQueue
    if (self->cmdQueue_) {
      self->drained_.clear();
      const SampleTime cutoff = self->sampleCounter_ + static_cast<SampleTime>(inNumberFrames);
      self->cmdQueue_->drainUpTo(cutoff, self->drained_);
      self->sampleCounter_ = cutoff;
      // Simple delivery: call handleEvent on all nodes for now (block-accurate)
      // Future: maintain id->node mapping instead of broadcasting
      if (!self->drained_.empty()) {
        // Very simple per-node delivery by id string match (optimize later)
        for (const Command& c : self->drained_) {
          self->graph_->forEachNode([&](const std::string& id, Node& n){
            if (c.nodeId && id == c.nodeId) n.handleEvent(c);
          });
        }
      }
    } else {
      self->sampleCounter_ += static_cast<SampleTime>(inNumberFrames);
    }
    self->graph_->process(ctx, interleaved, self->channels_);
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



