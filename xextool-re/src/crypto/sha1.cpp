// sha1.cpp - RFC 3174 SHA-1.
#include "sha1.h"
#include <cstring>

namespace xex::crypto {

static inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void Sha1::reset() {
    h_[0]=0x67452301; h_[1]=0xEFCDAB89; h_[2]=0x98BADCFE;
    h_[3]=0x10325476; h_[4]=0xC3D2E1F0;
    total_ = 0; buf_len_ = 0;
}

void Sha1::process(const uint8_t block[64]) {
    uint32_t w[80];
    for (int i=0;i<16;++i)
        w[i] = (uint32_t(block[i*4])<<24)|(uint32_t(block[i*4+1])<<16)|
               (uint32_t(block[i*4+2])<<8)|block[i*4+3];
    for (int i=16;i<80;++i) w[i] = rol(w[i-3]^w[i-8]^w[i-14]^w[i-16], 1);

    uint32_t a=h_[0],b=h_[1],c=h_[2],d=h_[3],e=h_[4];
    for (int i=0;i<80;++i) {
        uint32_t f,k;
        if      (i<20){ f=(b&c)|((~b)&d);        k=0x5A827999; }
        else if (i<40){ f=b^c^d;                 k=0x6ED9EBA1; }
        else if (i<60){ f=(b&c)|(b&d)|(c&d);     k=0x8F1BBCDC; }
        else         { f=b^c^d;                  k=0xCA62C1D6; }
        uint32_t t = rol(a,5)+f+e+k+w[i];
        e=d; d=c; c=rol(b,30); b=a; a=t;
    }
    h_[0]+=a; h_[1]+=b; h_[2]+=c; h_[3]+=d; h_[4]+=e;
}

void Sha1::update(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    total_ += len;
    if (buf_len_) {
        while (len && buf_len_ < 64) { buf_[buf_len_++] = *p++; --len; }
        if (buf_len_ == 64) { process(buf_); buf_len_ = 0; }
    }
    while (len >= 64) { process(p); p += 64; len -= 64; }
    while (len--) buf_[buf_len_++] = *p++;
}

std::array<uint8_t,20> Sha1::finish() {
    uint64_t bits = total_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (buf_len_ != 56) update(&zero, 1);
    uint8_t len_be[8];
    for (int i=0;i<8;++i) len_be[i] = uint8_t(bits >> (56 - 8*i));
    update(len_be, 8);

    std::array<uint8_t,20> out;
    for (int i=0;i<5;++i) {
        out[i*4]   = uint8_t(h_[i]>>24);
        out[i*4+1] = uint8_t(h_[i]>>16);
        out[i*4+2] = uint8_t(h_[i]>>8);
        out[i*4+3] = uint8_t(h_[i]);
    }
    return out;
}

} // namespace xex::crypto
