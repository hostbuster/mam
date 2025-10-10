#pragma once

#include <cstdint>

namespace KickParam {
  constexpr uint16_t F0 = 1;
  constexpr uint16_t FEND = 2;
  constexpr uint16_t PITCH_DECAY_MS = 3;
  constexpr uint16_t AMP_DECAY_MS = 4;
  constexpr uint16_t GAIN = 5;
  constexpr uint16_t CLICK = 6;
  constexpr uint16_t BPM = 7;
  constexpr uint16_t LOOP = 8;
}

namespace ClapParam {
  constexpr uint16_t AMP_DECAY_MS = 1;
  constexpr uint16_t GAIN = 2;
  constexpr uint16_t BPM = 3;
  constexpr uint16_t LOOP = 4;
}


