// selftest.cpp - known-answer tests for the crypto primitives.
#include "crypto/aes.h"
#include "crypto/sha1.h"
#include "crypto/keys.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string hex(const uint8_t* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (size_t i=0;i<n;++i){ s+=d[p[i]>>4]; s+=d[p[i]&0xF]; }
    return s;
}
bool expect(const char* name, const std::string& got, const std::string& want) {
    bool ok = got == want;
    std::printf("  [%s] %-22s %s\n", ok?"PASS":"FAIL", name, ok?"":(got+" != "+want).c_str());
    return ok;
}

} // namespace

int run_selftest() {
    using namespace xex::crypto;
    bool ok = true;

    // FIPS-197 AES-128 known-answer.
    uint8_t key[16], pt[16], ct[16], rt[16];
    for (int i=0;i<16;++i){ key[i]=uint8_t(i); }
    const uint8_t pt_v[16]={0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    std::memcpy(pt, pt_v, 16);
    Aes128 aes(key);
    aes.encrypt_block(pt, ct);
    ok &= expect("AES-128 encrypt", hex(ct,16), "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes.decrypt_block(ct, rt);
    ok &= expect("AES-128 decrypt", hex(rt,16), hex(pt_v,16));

    // CBC round-trip with zero IV.
    uint8_t data[32], iv[16]={0};
    for (int i=0;i<32;++i) data[i]=uint8_t(i*7);
    uint8_t orig[32]; std::memcpy(orig,data,32);
    aes.cbc_encrypt(data,32,iv);
    std::memset(iv,0,16);
    aes.cbc_decrypt(data,32,iv);
    ok &= expect("AES-128-CBC roundtrip", hex(data,32), hex(orig,32));

    // SHA-1 known answers.
    auto h1 = Sha1::hash("abc", 3);
    ok &= expect("SHA-1(\"abc\")", hex(h1.data(),20), "a9993e364706816aba3e25717850c26c9cd0d89d");
    auto h2 = Sha1::hash("", 0);
    ok &= expect("SHA-1(\"\")", hex(h2.data(),20), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    std::string ms(1000000, 'a');
    auto h3 = Sha1::hash(ms.data(), ms.size());
    ok &= expect("SHA-1(1e6 x 'a')", hex(h3.data(),20), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");

    // Embedded key linkage sanity check.
    ok &= expect("retail AES key", hex(kRetailKey,16), "20b185a59d28fdc340583fbb0896bf91");

    std::printf("\n%s\n", ok ? "All crypto self-tests passed." : "SELF-TESTS FAILED.");
    return ok ? 0 : 1;
}
