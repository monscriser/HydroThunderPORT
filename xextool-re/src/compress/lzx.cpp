// lzx.cpp - LZXD decompressor.
//
// Clean-room implementation of the LZX decode path per the documented format
// (MS-PATCH / the public mspack algorithm). Used for XEX "normal" compression.
// XEX streams do not use Intel-E8 call translation, so that step is omitted.
#include "lzx.h"
#include <stdexcept>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

static const bool kLzxDebug = std::getenv("LZX_DEBUG") != nullptr;
#define LZXDBG(...) do { if (kLzxDebug) std::fprintf(stderr, __VA_ARGS__); } while(0)

namespace xex::compress {
namespace {

constexpr int kMinMatch = 2;
constexpr int kNumChars = 256;
constexpr int kPretreeElements = 20;
constexpr int kAlignedElements = 8;
constexpr int kSecondaryLengths = 249;
constexpr int kMaxMainSymbols = kNumChars + 8 * 51;

enum BlockType { kVerbatim = 1, kAligned = 2, kUncompressed = 3 };

const uint32_t kPositionBase[51] = {
    0,1,2,3,4,6,8,12,16,24,32,48,64,96,128,192,256,384,512,768,1024,1536,2048,
    3072,4096,6144,8192,12288,16384,24576,32768,49152,65536,98304,131072,196608,
    262144,393216,524288,655360,786432,917504,1048576,1179648,1310720,1441792,
    1572864,1703936,1835008,1966080,2097152 };
const uint8_t kExtraBits[51] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,
    15,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17 };

// LZX bitstream: 16-bit little-endian words, bits consumed MSB-first.
class BitReader {
public:
    BitReader(const uint8_t* p, size_t n) : p_(p), end_(p + n) {}

    // Reads up to 17 bits at once (the accumulator is 32 bits, refilled 16 at a
    // time, so requesting more would force a negative shift in ensure()).
    uint32_t read(int n) {                 // n in [0,17]
        if (n == 0) return 0;
        ensure(n);
        uint32_t v = bitbuf_ >> (32 - n);
        bitbuf_ <<= n; bitsleft_ -= n;
        return v;
    }
    // Wider reads must be split into <=16-bit pieces, MSB-first.
    uint32_t read_big(int n) {
        if (n <= 16) return read(n);
        uint32_t hi = read(16);
        return (hi << (n - 16)) | read(n - 16);
    }
    void align16() {                       // align to next 16-bit boundary
        int rem = bitsleft_ & 15;
        if (rem) read(rem);
    }
    // After a 16-bit-aligned point, read a little-endian 32-bit value directly
    // from the underlying words (used by uncompressed blocks).
    uint32_t read_u32_le_direct() {
        uint16_t a = word();
        uint16_t b = word();
        return uint32_t(a) | (uint32_t(b) << 16);
    }
    const uint8_t* raw_ptr() const { return p_; }
    void set_raw_ptr(const uint8_t* p) { p_ = p; bitbuf_ = 0; bitsleft_ = 0; }
    // Rewind over buffered whole words so raw_ptr() is the next unconsumed byte.
    // Call only when 16-bit aligned (bitsleft_ a multiple of 16).
    void drain_to_byte() { p_ -= bitsleft_ / 8; bitsleft_ = 0; bitbuf_ = 0; }

private:
    uint16_t word() {
        uint16_t w = 0;
        if (p_ + 2 <= end_) w = uint16_t(p_[0] | (p_[1] << 8));
        p_ += 2;
        return w;
    }
    void ensure(int n) {
        while (bitsleft_ < n) {
            bitbuf_ |= uint32_t(word()) << (16 - bitsleft_);
            bitsleft_ += 16;
        }
    }
    const uint8_t* p_;
    const uint8_t* end_;
    uint32_t bitbuf_ = 0;
    int bitsleft_ = 0;
};

// Canonical Huffman decoder (RFC-1951 style, MSB-first to match the bitstream).
class Huffman {
public:
    // LZX code lengths range 0..16, so the count/offset tables need 17 slots.
    void build(const uint8_t* lengths, int n) {
        int maxlen = 0;
        for (int i = 0; i <= 16; ++i) count_[i] = 0;
        for (int i = 0; i < n; ++i) { count_[lengths[i]]++; if (lengths[i] > maxlen) maxlen = lengths[i]; }
        count_[0] = 0;
        maxlen_ = maxlen;
        int offsets[18]; offsets[1] = 0;
        for (int l = 1; l <= 16; ++l) offsets[l + 1] = offsets[l] + count_[l];
        symbols_.assign(n, 0);
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbols_[offsets[lengths[i]]++] = uint16_t(i);
    }
    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= maxlen_; ++len) {
            code |= int(br.read(1));
            int cnt = count_[len];
            if (code - first < cnt) return symbols_[index + (code - first)];
            index += cnt; first += cnt; first <<= 1; code <<= 1;
        }
        throw std::runtime_error("lzx: bad huffman code");
    }
private:
    int count_[17] = {0};
    int maxlen_ = 0;
    std::vector<uint16_t> symbols_;
};

int num_position_slots(uint32_t window_size) {
    int slots = 0;
    while (slots < 51 && kPositionBase[slots] < window_size) ++slots;
    return slots; // index past the last base < window_size
}

// Decode delta-coded code lengths for [first,last) using a 20-symbol pretree.
void read_lengths(BitReader& br, uint8_t* lengths, int first, int last) {
    uint8_t pre_len[kPretreeElements];
    for (int i = 0; i < kPretreeElements; ++i) pre_len[i] = uint8_t(br.read(4));
    Huffman pretree; pretree.build(pre_len, kPretreeElements);

    int i = first;
    while (i < last) {
        int sym = pretree.decode(br);
        if (sym == 17) {                       // run of zeros
            int run = int(br.read(4)) + 4;
            while (run-- && i < last) lengths[i++] = 0;
        } else if (sym == 18) {                // longer run of zeros
            int run = int(br.read(5)) + 20;
            while (run-- && i < last) lengths[i++] = 0;
        } else if (sym == 19) {                // run of identical lengths
            int run = int(br.read(1)) + 4;
            int s = pretree.decode(br);
            int v = (lengths[i] - s + 17) % 17;
            while (run-- && i < last) lengths[i++] = uint8_t(v);
        } else {                               // single delta-coded length
            int v = (lengths[i] - sym + 17) % 17;
            lengths[i++] = uint8_t(v);
        }
    }
}

// Reverse the Intel-E8 call-instruction translation, in 32 KB frames. For each
// 0xE8 byte, the following 32-bit little-endian operand is converted from the
// stored relative form back to its original value, using its absolute position.
void undo_e8(uint8_t* data, size_t size, int32_t filesize) {
    if (filesize == 0) return;
    constexpr size_t kFrame = 32768;
    size_t frame_start = 0;
    int frames = 0;
    while (frame_start < size && frames < 32768) {
        size_t frame_size = std::min(kFrame, size - frame_start);
        if (frame_size > 10) {
            uint8_t* d = data + frame_start;
            size_t end = frame_size - 10;
            for (size_t i = 0; i < end; ) {
                if (d[i] != 0xE8) { ++i; continue; }
                int32_t pos = int32_t(frame_start + i);
                int32_t v = int32_t(uint32_t(d[i+1]) | (uint32_t(d[i+2]) << 8) |
                                    (uint32_t(d[i+3]) << 16) | (uint32_t(d[i+4]) << 24));
                if (v >= -pos && v < filesize) {
                    int32_t nv = (v >= 0) ? v - pos : v + filesize;
                    d[i+1] = uint8_t(nv);       d[i+2] = uint8_t(nv >> 8);
                    d[i+3] = uint8_t(nv >> 16); d[i+4] = uint8_t(nv >> 24);
                }
                i += 5;
            }
        }
        frame_start += kFrame;
        ++frames;
    }
}

} // namespace

std::vector<uint8_t> lzx_decompress(const uint8_t* in, size_t in_size,
                                    size_t out_size, uint32_t window_size,
                                    const uint8_t* ref_data, size_t ref_len) {
    std::vector<uint8_t> out;
    out.reserve(size_t(window_size) + out_size);
    // For LZX-DELTA, pre-seed the dictionary exactly as the encoder's window:
    // [zero padding][reference data], filling the whole window. Matches may
    // reference anywhere in it (including the zero padding); we return only the
    // newly produced bytes afterwards.
    if (ref_data && ref_len) {
        size_t padding = (window_size > ref_len) ? window_size - ref_len : 0;
        out.assign(padding, 0);
        out.insert(out.end(), ref_data, ref_data + ref_len);
    }
    const size_t base = out.size();

    const int pos_slots = num_position_slots(window_size);
    const int main_symbols = kNumChars + 8 * pos_slots;

    std::vector<uint8_t> main_len(main_symbols, 0);
    std::vector<uint8_t> length_len(kSecondaryLengths, 0);
    Huffman main_tree, length_tree, aligned_tree;

    BitReader br(in, in_size);

    // Stream header: 1 bit = Intel-E8 (x86 call) translation present, then a
    // 32-bit file size. Most XEX streams have it off, but some (e.g. older
    // system builds) enable it, so it must be honored.
    int32_t e8_filesize = 0;
    if (br.read(1)) e8_filesize = int32_t(br.read_big(32));

    constexpr size_t kFrameSize = 32768;
    uint32_t R0 = 1, R1 = 1, R2 = 1;
    int block_type = 0;
    uint32_t block_remaining = 0;   // bytes left in the current block

    // The output is decoded in 32 KB frames; the bitstream is realigned to a
    // 16-bit boundary at the end of each frame. Block state (type, trees,
    // repeated offsets) persists across frame and block boundaries. Frame and
    // size counting is relative to `base` (so a pre-seeded reference window
    // doesn't shift the frame boundaries).
    while (out.size() - base < out_size) {
        size_t cur = out.size() - base;
        size_t fe = cur - (cur % kFrameSize) + kFrameSize;
        if (fe > out_size) fe = out_size;
        size_t frame_end = base + fe;

        while (out.size() < frame_end) {
            if (block_remaining == 0) {
                block_type = int(br.read(3));
                block_remaining = br.read_big(24);
                LZXDBG("block @out=%zu type=%d size=%u\n", out.size(), block_type, block_remaining);
                if (block_type == kAligned) {
                    uint8_t al[kAlignedElements];
                    for (int i = 0; i < kAlignedElements; ++i) al[i] = uint8_t(br.read(3));
                    aligned_tree.build(al, kAlignedElements);
                }
                if (block_type == kVerbatim || block_type == kAligned) {
                    read_lengths(br, main_len.data(), 0, kNumChars);
                    read_lengths(br, main_len.data(), kNumChars, main_symbols);
                    main_tree.build(main_len.data(), main_symbols);
                    read_lengths(br, length_len.data(), 0, kSecondaryLengths);
                    length_tree.build(length_len.data(), kSecondaryLengths);
                } else if (block_type == kUncompressed) {
                    br.align16();
                    br.drain_to_byte();
                    const uint8_t* p = br.raw_ptr();
                    R0 = uint32_t(p[0]|(p[1]<<8)|(p[2]<<16)|(uint32_t(p[3])<<24));
                    R1 = uint32_t(p[4]|(p[5]<<8)|(p[6]<<16)|(uint32_t(p[7])<<24));
                    R2 = uint32_t(p[8]|(p[9]<<8)|(p[10]<<16)|(uint32_t(p[11])<<24));
                    br.set_raw_ptr(p + 12);
                } else {
                    throw std::runtime_error("lzx: bad block type");
                }
            }

            size_t want = frame_end - out.size();
            if (uint32_t(want) > block_remaining) want = block_remaining;
            const size_t target = out.size() + want;

            if (block_type == kUncompressed) {
                const uint8_t* p = br.raw_ptr();
                while (out.size() < target) out.push_back(*p++);
                br.set_raw_ptr(p);
                block_remaining -= uint32_t(want);
                if (block_remaining == 0 && (br.raw_ptr() - in) & 1)
                    br.set_raw_ptr(br.raw_ptr() + 1); // even-byte pad at block end
                continue;
            }

            while (out.size() < target) {
                int sym = main_tree.decode(br);
                if (sym < kNumChars) { out.push_back(uint8_t(sym)); continue; }
                sym -= kNumChars;
                int length_footer = sym & 7;
                int position_slot = sym >> 3;
                int match_len = (length_footer == 7)
                                ? length_tree.decode(br) + 7 + kMinMatch
                                : length_footer + kMinMatch;

                uint32_t match_offset;
                if (position_slot == 0)      { match_offset = R0; }
                else if (position_slot == 1) { match_offset = R1; R1 = R0; R0 = match_offset; }
                else if (position_slot == 2) { match_offset = R2; R2 = R0; R0 = match_offset; }
                else {
                    int extra = kExtraBits[position_slot];
                    uint32_t formatted;
                    if (block_type == kAligned && extra >= 3) {
                        uint32_t verbatim = br.read(extra - 3) << 3;
                        uint32_t aligned = uint32_t(aligned_tree.decode(br));
                        formatted = verbatim + aligned;
                    } else {
                        formatted = br.read(extra);
                    }
                    match_offset = kPositionBase[position_slot] - 2 + formatted;
                    R2 = R1; R1 = R0; R0 = match_offset;
                }

                if (match_offset == 0 || match_offset > out.size())
                    throw std::runtime_error("lzx: invalid match offset");
                size_t src = out.size() - match_offset;
                for (int k = 0; k < match_len; ++k) out.push_back(out[src + k]);
            }
            block_remaining -= uint32_t(want);
        }

        if (out.size() - base < out_size) br.align16();
    }

    // Undo Intel-E8 translation on the newly produced bytes, if it was enabled.
    if (e8_filesize) undo_e8(out.data() + base, out_size, e8_filesize);

    // Return only the newly produced bytes (drop the pre-seeded reference).
    return std::vector<uint8_t>(out.begin() + base, out.begin() + base + out_size);
}

} // namespace xex::compress
