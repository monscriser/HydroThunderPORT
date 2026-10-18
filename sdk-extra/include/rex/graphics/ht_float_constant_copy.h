#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

namespace rex::graphics::ht {

// Each bitmap bit selects one float4, packed in ascending register order.
inline uint8_t* CopyFloatConstantsScalar(uint8_t* dst, const uint32_t* src,
                                         const uint64_t* bitmap) {
  for (uint32_t word = 0; word < 4; ++word) {
    uint64_t mask = bitmap[word];
    while (mask) {
      const uint32_t first = std::countr_zero(mask);
      mask &= mask - 1;
      std::memcpy(dst, src + word * 256 + first * 4, 16);
      dst += 16;
    }
  }
  return dst;
}

inline uint8_t* CopyFloatConstantsRuns(uint8_t* dst, const uint32_t* src,
                                       const uint64_t* bitmap) {
  for (uint32_t word = 0; word < 4; ++word) {
    uint64_t mask = bitmap[word];
    // Sparse maps keep the original fixed-size vector copies.
    if (!(mask & (mask >> 1))) {
      while (mask) {
        const uint32_t first = std::countr_zero(mask);
        mask &= mask - 1;
        std::memcpy(dst, src + word * 256 + first * 4, 16);
        dst += 16;
      }
      continue;
    }
    while (mask) {
      const uint32_t first = std::countr_zero(mask);
      const uint32_t count = std::countr_one(mask >> first);
      const size_t bytes = size_t(count) * 16;
      std::memcpy(dst, src + word * 256 + first * 4, bytes);
      dst += bytes;
      // Unsigned carry clears this run, including a run ending at bit 63.
      mask &= mask + (UINT64_C(1) << first);
    }
  }
  return dst;
}

}  // namespace rex::graphics::ht
