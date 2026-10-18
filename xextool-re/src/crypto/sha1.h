// sha1.h - SHA-1 (XEX uses it for page/header hashes and RSA signatures).
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>

namespace xex::crypto {

class Sha1 {
public:
    Sha1() { reset(); }
    void reset();
    void update(const void* data, size_t len);
    std::array<uint8_t, 20> finish();

    static std::array<uint8_t, 20> hash(const void* data, size_t len) {
        Sha1 s; s.update(data, len); return s.finish();
    }

private:
    void process(const uint8_t block[64]);
    uint32_t h_[5];
    uint64_t total_ = 0;
    uint8_t  buf_[64];
    size_t   buf_len_ = 0;
};

} // namespace xex::crypto
