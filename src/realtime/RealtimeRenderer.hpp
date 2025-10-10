#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <cstdint>
#include <stdexcept>
#include "../core/ScopedAudioUnit.hpp"

template <typename Synth>
class RealtimeRenderer {
public:
  RealtimeRenderer() = default;
  ~RealtimeRenderer() { stop(); }

  void start(Synth* synth, double requestedSampleRate = 48000.0) {
    if (!synth) throw std::invalid_argument("synth is null");
    synth_ = synth;

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp) throw std::runtime_error("Default output component not found");

    OSStatus err = AudioComponentInstanceNew(comp, unit_.ptr());
    if (err != noErr) throw std::runtime_error("AudioComponentInstanceNew failed");

    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = requestedSampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
    asbd.mBitsPerChannel = 32;
    asbd.mChannelsPerFrame = 2;
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = 4;
    asbd.mBytesPerPacket = 4;

    err = AudioUnitSetProperty(unit_.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, sizeof(asbd));
    if (err != noErr) throw std::runtime_error("AudioUnitSetProperty(StreamFormat) failed");

    AURenderCallbackStruct cb{};
    cb.inputProc = &RealtimeRenderer::render;
    cb.inputProcRefCon = this;
    err = AudioUnitSetProperty(unit_.get(), kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Global, 0, &cb, sizeof(cb));
    if (err != noErr) throw std::runtime_error("AudioUnitSetProperty(SetRenderCallback) failed");

    err = AudioUnitInitialize(unit_.get());
    if (err != noErr) throw std::runtime_error("AudioUnitInitialize failed");

    // Get actual device sample rate
    UInt32 size = sizeof(asbd);
    err = AudioUnitGetProperty(unit_.get(), kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, &size);
    if (err == noErr && asbd.mSampleRate > 0.0) {
      synth_->setSampleRate(asbd.mSampleRate);
    }

    err = AudioOutputUnitStart(unit_.get());
    if (err != noErr) throw std::runtime_error("AudioOutputUnitStart failed");
    running_ = true;
  }

  void stop() {
    unit_.release();
    running_ = false;
  }

private:
  static OSStatus render(void* inRefCon, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 inNumberFrames, AudioBufferList* ioData) noexcept {
    auto* self = static_cast<RealtimeRenderer*>(inRefCon);
    float* ch0 = static_cast<float*>(ioData->mBuffers[0].mData);
    float* ch1 = static_cast<float*>(ioData->mBuffers[1].mData);
    for (UInt32 i = 0; i < inNumberFrames; ++i) {
      const float s = self->synth_->process();
      ch0[i] = s;
      ch1[i] = s;
    }
    return noErr;
  }

  AudioUnitHandle unit_{};
  Synth* synth_ = nullptr;
  bool running_ = false;
};



