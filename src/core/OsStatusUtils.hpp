#pragma once

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <string>

inline std::string osstatusToString(OSStatus status) {
  // If it's a 4-char code, render it as such
  char str[16];
  *(UInt32*)(str) = CFSwapInt32HostToBig(static_cast<UInt32>(status));
  if (isprint(str[0]) && isprint(str[1]) && isprint(str[2]) && isprint(str[3])) {
    str[4] = '\0';
    return std::string("'") + str + "'";
  }
  return std::to_string(status);
}


