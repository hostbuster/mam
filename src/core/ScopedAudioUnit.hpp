#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

class AudioUnitHandle {
public:
  AudioUnitHandle() = default;
  ~AudioUnitHandle() { release(); }
  AudioUnitHandle(const AudioUnitHandle&) = delete;
  AudioUnitHandle& operator=(const AudioUnitHandle&) = delete;

  AudioUnit* ptr() { return &unit_; }
  AudioUnit get() const { return unit_; }
  bool valid() const { return unit_ != nullptr; }

  void release() {
    if (unit_) {
      AudioOutputUnitStop(unit_);
      AudioUnitUninitialize(unit_);
      AudioComponentInstanceDispose(unit_);
      unit_ = nullptr;
    }
  }

private:
  AudioUnit unit_ = nullptr;
};


