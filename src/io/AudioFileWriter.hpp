#pragma once

#include <AudioToolbox/ExtendedAudioFile.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string>
#include <vector>
#include <stdexcept>

enum class FileFormat { Wav, Aiff, Caf };
enum class BitDepth { Pcm16, Pcm24, Float32 };

struct AudioFileSpec {
  FileFormat format = FileFormat::Wav;
  BitDepth bitDepth = BitDepth::Float32;
  uint32_t sampleRate = 48000;
  uint32_t channels = 2;
};

inline OSType toFileType(FileFormat f) {
  switch (f) {
    case FileFormat::Wav: return kAudioFileWAVEType;
    case FileFormat::Aiff: return kAudioFileAIFFType;
    case FileFormat::Caf: return kAudioFileCAFType;
  }
  return kAudioFileWAVEType;
}

inline void writeWithExtAudioFile(const std::string& path, const AudioFileSpec& spec, const std::vector<float>& interleaved) {
  AudioStreamBasicDescription src{};
  src.mSampleRate = spec.sampleRate;
  src.mFormatID = kAudioFormatLinearPCM;
  src.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
  src.mBitsPerChannel = 32;
  src.mChannelsPerFrame = spec.channels;
  src.mFramesPerPacket = 1;
  src.mBytesPerFrame = 4 * spec.channels;
  src.mBytesPerPacket = src.mBytesPerFrame;

  AudioStreamBasicDescription dst = src;
  if (spec.bitDepth == BitDepth::Pcm16) {
    dst.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    dst.mBitsPerChannel = 16;
    dst.mBytesPerFrame = (dst.mBitsPerChannel / 8) * spec.channels;
    dst.mBytesPerPacket = dst.mBytesPerFrame;
  } else if (spec.bitDepth == BitDepth::Pcm24) {
    dst.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    dst.mBitsPerChannel = 24;
    dst.mBytesPerFrame = (dst.mBitsPerChannel / 8) * spec.channels;
    dst.mBytesPerPacket = dst.mBytesPerFrame;
  } else {
    dst = src; // Float32
  }

  // For AIFF integer formats, force big-endian
  if (spec.format == FileFormat::Aiff && spec.bitDepth != BitDepth::Float32) {
    dst.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian;
  }

  ExtAudioFileRef file = nullptr;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.size()), false);
  if (!url) {
    throw std::runtime_error("CFURLCreateFromFileSystemRepresentation failed");
  }
  OSStatus err = ExtAudioFileCreateWithURL(url, toFileType(spec.format), &dst, nullptr, kAudioFileFlags_EraseFile, &file);
  CFRelease(url);
  if (err != noErr) {
    throw std::runtime_error("ExtAudioFileCreateWithURL failed");
  }

  err = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(src), &src);
  if (err != noErr) {
    ExtAudioFileDispose(file);
    throw std::runtime_error("ExtAudioFileSetProperty(ClientDataFormat) failed");
  }

  AudioBufferList buf{};
  buf.mNumberBuffers = 1;
  buf.mBuffers[0].mNumberChannels = spec.channels;
  buf.mBuffers[0].mDataByteSize = (UInt32)(interleaved.size() * sizeof(float));
  buf.mBuffers[0].mData = const_cast<float*>(interleaved.data());

  UInt32 frames = static_cast<UInt32>(interleaved.size() / spec.channels);
  err = ExtAudioFileWrite(file, frames, &buf);
  ExtAudioFileDispose(file);
  if (err != noErr) {
    throw std::runtime_error("ExtAudioFileWrite failed");
  }
}


