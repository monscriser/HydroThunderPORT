// keys.h - embedded Xbox 360 key material (see keys.cpp).
#pragma once
#include <cstdint>

namespace xex::crypto {

// AES-128 keys that wrap the per-title basefile key in the security info.
extern const uint8_t kRetailKey[16]; // XEX2 retail
extern const uint8_t kXex1Key[16];   // older XEX1

// 2048-bit RSA public keys used to verify XEX header signatures. The big-endian
// modulus is 256 bytes; the Xbox 360 XECRYPT structures store the words in a
// reversed order which the verifier accounts for.
struct RsaPublicKey {
    const uint8_t* modulus; // 256 bytes, as laid out in the binary
    uint32_t       exponent;
};
extern const RsaPublicKey kRsaPublicKeys[3];

} // namespace xex::crypto
