// aes.h - minimal AES-128 (the only key size XEX uses).
//
// Self-contained, no external dependencies. Provides single-block ECB (used to
// unwrap the title key) and CBC (used for the basefile, with a zero IV).
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace xex::crypto {

class Aes128 {
public:
    explicit Aes128(const uint8_t key[16]);

    void encrypt_block(const uint8_t in[16], uint8_t out[16]) const;
    void decrypt_block(const uint8_t in[16], uint8_t out[16]) const;

    // CBC over a buffer whose length must be a multiple of 16. iv is updated
    // to the last processed block so calls can be chained; pass a 16-byte zero
    // IV for XEX basefiles. In/out may alias.
    void cbc_encrypt(uint8_t* data, size_t len, uint8_t iv[16]) const;
    void cbc_decrypt(uint8_t* data, size_t len, uint8_t iv[16]) const;

private:
    uint8_t enc_keys_[176]; // 11 round keys
    uint8_t dec_keys_[176];
};

} // namespace xex::crypto
