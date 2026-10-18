// byteio.h - big-endian readers over an in-memory buffer.
//
// XEX files store all multi-byte integers big-endian (the Xbox 360 is a
// big-endian PowerPC machine). We run on little-endian x86, so every field
// is byte-swapped on read. These helpers keep that explicit and bounds-checked.
#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace xex {

// A non-owning view over a byte buffer with bounds-checked big-endian reads.
class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}
    explicit Reader(const std::vector<uint8_t>& v) : data_(v.data()), size_(v.size()) {}

    size_t size() const { return size_; }
    const uint8_t* data() const { return data_; }

    uint8_t  u8 (size_t off) const { check(off, 1); return data_[off]; }
    uint16_t u16(size_t off) const { check(off, 2);
        return (uint16_t(data_[off]) << 8) | data_[off + 1]; }
    uint32_t u32(size_t off) const { check(off, 4);
        return (uint32_t(data_[off]) << 24) | (uint32_t(data_[off + 1]) << 16) |
               (uint32_t(data_[off + 2]) << 8) | data_[off + 3]; }
    uint64_t u64(size_t off) const {
        return (uint64_t(u32(off)) << 32) | u32(off + 4); }

    // Fixed-length field copied out as raw bytes.
    std::vector<uint8_t> bytes(size_t off, size_t n) const {
        check(off, n);
        return std::vector<uint8_t>(data_ + off, data_ + off + n);
    }

    // NUL-terminated (or length-bounded) ASCII string starting at off.
    std::string cstr(size_t off, size_t maxlen) const {
        std::string s;
        for (size_t i = 0; i < maxlen && off + i < size_; ++i) {
            uint8_t c = data_[off + i];
            if (c == 0) break;
            s.push_back(char(c));
        }
        return s;
    }

    bool in_bounds(size_t off, size_t n) const {
        return off <= size_ && n <= size_ - off;
    }

private:
    void check(size_t off, size_t n) const {
        if (!in_bounds(off, n))
            throw std::out_of_range("xex::Reader read out of bounds at offset " +
                                    std::to_string(off));
    }
    const uint8_t* data_;
    size_t size_;
};

} // namespace xex
