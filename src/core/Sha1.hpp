#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Minimal SHA-1 implementation for hashing rendered audio buffers.
// Computes SHA-1 over arbitrary bytes and returns lowercase hex string.
namespace sha1_detail {
  struct Ctx {
    uint32_t h[5];
    uint64_t lenBits;
    uint8_t  buf[64];
    size_t   bufUsed;
  };

  inline uint32_t rol(uint32_t x, uint32_t n) { return (x << n) | (x >> (32 - n)); }

  inline void init(Ctx& c) {
    c.h[0] = 0x67452301u; c.h[1] = 0xEFCDAB89u; c.h[2] = 0x98BADCFEu; c.h[3] = 0x10325476u; c.h[4] = 0xC3D2E1F0u;
    c.lenBits = 0; c.bufUsed = 0; std::memset(c.buf, 0, sizeof(c.buf));
  }

  inline void processBlock(Ctx& c, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(p[4*i]) << 8*3)
           | (static_cast<uint32_t>(p[4*i+1]) << 8*2)
           | (static_cast<uint32_t>(p[4*i+2]) << 8*1)
           | (static_cast<uint32_t>(p[4*i+3]) << 0);
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a=c.h[0], b=c.h[1], c2=c.h[2], d=c.h[3], e=c.h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f,k;
      if (i < 20)      { f = (b & c2) | ((~b) & d); k = 0x5A827999u; }
      else if (i < 40) { f = b ^ c2 ^ d;               k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (b & c2) | (b & d) | (c2 & d); k = 0x8F1BBCDCu; }
      else             { f = b ^ c2 ^ d;               k = 0xCA62C1D6u; }
      uint32_t temp = rol(a,5) + f + e + k + w[i];
      e = d; d = c2; c2 = rol(b,30); b = a; a = temp;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += c2; c.h[3] += d; c.h[4] += e;
  }

  inline void update(Ctx& c, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    c.lenBits += static_cast<uint64_t>(len) * 8ull;
    while (len > 0) {
      size_t take = std::min(len, 64 - c.bufUsed);
      std::memcpy(c.buf + c.bufUsed, p, take);
      c.bufUsed += take; p += take; len -= take;
      if (c.bufUsed == 64) { processBlock(c, c.buf); c.bufUsed = 0; }
    }
  }

  inline void finalize(Ctx& c, uint8_t out[20]) {
    // append 0x80 then zeros then 64-bit length
    c.buf[c.bufUsed++] = 0x80;
    if (c.bufUsed > 56) { while (c.bufUsed < 64) c.buf[c.bufUsed++] = 0; processBlock(c, c.buf); c.bufUsed = 0; }
    while (c.bufUsed < 56) c.buf[c.bufUsed++] = 0;
    for (int i = 7; i >= 0; --i) c.buf[c.bufUsed++] = static_cast<uint8_t>((c.lenBits >> (i*8)) & 0xFF);
    processBlock(c, c.buf);
    for (int i = 0; i < 5; ++i) {
      out[4*i+0] = static_cast<uint8_t>((c.h[i] >> 24) & 0xFF);
      out[4*i+1] = static_cast<uint8_t>((c.h[i] >> 16) & 0xFF);
      out[4*i+2] = static_cast<uint8_t>((c.h[i] >> 8) & 0xFF);
      out[4*i+3] = static_cast<uint8_t>((c.h[i] >> 0) & 0xFF);
    }
  }
}

inline std::string computeSha1Hex(const void* data, size_t len) {
  sha1_detail::Ctx c; sha1_detail::init(c);
  sha1_detail::update(c, data, len);
  uint8_t dig[20]; sha1_detail::finalize(c, dig);
  static const char* hex = "0123456789abcdef";
  std::string out; out.reserve(40);
  for (int i = 0; i < 20; ++i) { out.push_back(hex[(dig[i] >> 4) & 0xF]); out.push_back(hex[dig[i] & 0xF]); }
  return out;
}


