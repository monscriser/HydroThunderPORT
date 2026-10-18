// lzx_encode.h - Microsoft LZX *compressor* as implemented by xextool.exe.
//
// This is a faithful transliteration of xextool's statically-linked MS-LZX
// optimal-parse encoder (RE'd from xextool_unpacked.exe, function cluster
// 0x4203c0-0x424dd0). It is bit-for-bit deterministic, so the goal is output
// byte-identical to the original tool's `-c c` / `-r` / `-m` write path.
//
// The original stores raw pointers inside a 32-bit context arena and does
// biased-pointer arithmetic; to stay byte-exact AND portable to 64-bit we model
// the whole address space the codec touches as ONE arena and represent every
// "pointer" as a 32-bit signed byte-offset into it (see lzx_encode.cpp).
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace xex::compress {

// Compress `in_size` bytes with a 32 KB LZX window, producing the raw LZX
// bitstream exactly as xextool would (the chunk/block XEX framing is applied by
// the caller). `window_size` must be a power of two >= 0x8000.
//
// E8 (Intel call) translation is applied when `e8_filesize` != 0 (matching the
// XEX header's "image base size" used by the original); pass 0 to disable.
std::vector<uint8_t> lzx_compress(const uint8_t* in, size_t in_size,
                                  uint32_t window_size = 0x8000,
                                  uint32_t e8_filesize = 0);

} // namespace xex::compress
