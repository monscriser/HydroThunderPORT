// Verify the C++ devkit signer reproduces xextool's HeaderHash + signature.
#include "../src/crypto/xex_sign.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "C:/xt/out.xex";
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("cannot open %s\n", path); return 2; }
    std::fseek(f, 0, SEEK_END); long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> all(sz); if (std::fread(all.data(), 1, sz, f) != (size_t)sz) {}
    std::fclose(f);

    uint32_t data_offset = (uint32_t(all[8]) << 24) | (uint32_t(all[9]) << 16) |
                           (uint32_t(all[10]) << 8) | all[11];
    std::printf("data_offset = 0x%X\n", data_offset);

    std::vector<uint8_t> exp_hh(all.begin() + 0x284, all.begin() + 0x298);
    std::vector<uint8_t> exp_sig(all.begin() + 0x128, all.begin() + 0x228);

    std::vector<uint8_t> hdr(all.begin(), all.begin() + data_offset);
    xex::crypto::devkit_sign(hdr, data_offset);

    bool hh = std::equal(hdr.begin() + 0x284, hdr.begin() + 0x298, exp_hh.begin());
    bool sg = std::equal(hdr.begin() + 0x128, hdr.begin() + 0x228, exp_sig.begin());
    std::printf("HeaderHash: %s\n", hh ? "OK" : "FAIL");
    std::printf("Signature : %s\n", sg ? "*** OK ***" : "FAIL");
    if (!sg) {
        std::printf("  mine: "); for (int i=0;i<12;i++) std::printf("%02x", hdr[0x128+i]);
        std::printf("\n  xexm: "); for (int i=0;i<12;i++) std::printf("%02x", exp_sig[i]);
        std::printf("\n");
    }
    return (hh && sg) ? 0 : 1;
}
