// lzx.h - LZX (Microsoft LZX, as used by XEX) decompression.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace xex::compress {

// Decompress an LZX stream into exactly out_size bytes.
//   in          : concatenated LZX bitstream (block/chunk framing already removed)
//   window_size : LZX window in bytes (power of two), from the XEX header
//   ref_data/ref_len : optional reference window (LZX-DELTA patches). When given,
//     the stream is decoded against this pre-seeded dictionary; matches may
//     reference back into it. Only the newly produced out_size bytes are returned.
// Throws std::runtime_error on malformed input. No Intel-E8 preprocessing
// (XEX does not use it).
std::vector<uint8_t> lzx_decompress(const uint8_t* in, size_t in_size,
                                    size_t out_size, uint32_t window_size,
                                    const uint8_t* ref_data = nullptr,
                                    size_t ref_len = 0);

// Compress `in` into a continuous LZX bitstream (block/SHA framing NOT applied).
// Byte-for-byte reproduction of xorloser's xextool encoder. window_size is the
// LZX window (xextool uses 0x8000); e8_filesize enables Intel-E8 translation
// when non-zero (0 = disabled, as XEX game basefiles use).
std::vector<uint8_t> lzx_compress(const uint8_t* in, size_t in_size,
                                  uint32_t window_size, uint32_t e8_filesize);

} // namespace xex::compress
