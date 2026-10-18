// xex_sign.h - XeCrypt devkit XEX header signing (byte-identical to xextool).
#pragma once
#include <cstdint>
#include <vector>

namespace xex::crypto {

// Fill the HeaderHash (image-info +0x15C, file 0x284) and, when do_sign is set,
// the RSA signature (image-info +0x00, file 0x128) into a XEX header. `hdr` is
// the full header region [0, data_offset). Reproduces xorloser's xextool:
//   HeaderHash = SHA1( hdr[0x29c:data_offset] || hdr[0:0x128] )      (always)
//   digest     = XeCryptRotSumSha( hdr[0x228:0x29c] )
//   PSS        = EMSA-PSS(digest, salt="XBOX360XEX", RC4 keystream mask)
//   sig        = ((PSS * R^2) ^ d) mod N   (R=2^2048), stored qword-reversed
// When do_sign is false (retail output, no usable private key), the signature
// field is zeroed instead — matching xextool's behaviour.
void devkit_sign(std::vector<uint8_t>& hdr, uint32_t data_offset, bool do_sign = true,
                 uint32_t sec_off = 0x120);

// True if the 256-byte signature verifies against the embedded devkit public
// key (e=3): recovers a valid PSS block (0xBC trailer, top bit clear). Used to
// tell devkit- from retail-signed XEXs.
bool verify_devkit_sig(const uint8_t* sig256);

} // namespace xex::crypto
