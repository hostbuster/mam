#pragma once

// Simple base interface for instruments; can be expanded later with prepare(), setParam(), etc.
class Instrument {
public:
  virtual ~Instrument() = default;
  virtual void setSampleRate(double) = 0;
  virtual void reset() = 0;
  virtual float process() = 0;
};



