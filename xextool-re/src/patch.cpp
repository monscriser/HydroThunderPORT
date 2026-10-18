// patch.cpp - apply a .xexp delta patch to a base XEX.
//
// Ported from the documented algorithm in Xenia / XenonRecomp
// (xex_patcher.cpp), using this project's own LZX, AES and SHA-1. Produces a
// decrypted, uncompressed standalone XEX (the original tool's -u behavior).
#include "patch.h"
#include "compress/lzx.h"
#include "crypto/aes.h"
#include "crypto/keys.h"
#include "crypto/sha1.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

static const bool kPdbg = std::getenv("XEXP_DEBUG") != nullptr;
#define PDBG(...) do { if (kPdbg) std::fprintf(stderr, __VA_ARGS__); } while(0)

namespace xex {
namespace {

uint32_t be32(const uint8_t* d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o+1]) << 16) | (uint32_t(d[o+2]) << 8) | d[o+3];
}
uint16_t be16(const uint8_t* d, size_t o) { return uint16_t((d[o] << 8) | d[o+1]); }
void wr16(uint8_t* d, size_t o, uint16_t v) { d[o] = uint8_t(v >> 8); d[o+1] = uint8_t(v); }

constexpr uint32_t kHdrFileFormat   = 0x000003FF;
constexpr uint32_t kHdrDeltaPatch   = 0x000005FF;
constexpr size_t   kNotFound        = SIZE_MAX;

// Offset within `m` of an optional header's payload (for size-prefixed 0xFF
// headers, which is all this code needs). kNotFound if absent.
size_t opt_header_offset(const uint8_t* m, uint32_t key) {
    uint32_t count = be32(m, 0x14);
    for (uint32_t i = 0; i < count; ++i) {
        if (be32(m, 0x18 + i*8) == key) return be32(m, 0x18 + i*8 + 4);
    }
    return kNotFound;
}

[[noreturn]] void fail(const char* why) { throw std::runtime_error(std::string("patch: ") + why); }

// Apply one stream of delta-patch records to dst.
void lzx_delta_apply(const uint8_t* patch, uint32_t patchLen, uint32_t windowSize, uint8_t* dst) {
    const uint8_t* end = patch + patchLen;
    const uint8_t* cur = patch;
    while (cur < end) {
        uint32_t oldAddr = be32(cur, 0), newAddr = be32(cur, 4);
        uint16_t uncLen  = be16(cur, 8), cmpLen  = be16(cur, 10);
        if (cmpLen == 0 && uncLen == 0 && newAddr == 0 && oldAddr == 0) break; // end marker
        PDBG("  rec old=0x%X new=0x%X unc=0x%X cmp=0x%X\n", oldAddr, newAddr, uncLen, cmpLen);

        if (cmpLen == 0) {
            std::memset(dst + newAddr, 0, uncLen);
            cur += 12;
        } else if (cmpLen == 1) {
            std::memmove(dst + newAddr, dst + oldAddr, uncLen);
            cur += 12;
        } else {
            auto outv = compress::lzx_decompress(cur + 12, cmpLen, uncLen, windowSize,
                                                 dst + oldAddr, uncLen);
            std::memcpy(dst + newAddr, outv.data(), uncLen);
            cur += 12 + cmpLen;
        }
    }
}

} // namespace

std::vector<uint8_t> apply_patch(const std::vector<uint8_t>& base, const std::vector<uint8_t>& patch) {
    const uint8_t* xexBytes = base.data();
    const size_t xexBytesSize = base.size();
    const uint8_t* patchBytes = patch.data();
    const size_t patchBytesSize = patch.size();

    if (xexBytesSize < 0x18 || std::memcmp(xexBytes, "XEX2", 4) != 0) fail("base is not a XEX2 file");
    if (patchBytesSize < 0x18 || std::memcmp(patchBytes, "XEX2", 4) != 0) fail("patch is not a XEX2 file");

    uint32_t patchModuleFlags = be32(patchBytes, 4);
    if ((patchModuleFlags & (0x10 | 0x20 | 0x40)) == 0) fail("not a patch file");

    size_t patchDescOff = opt_header_offset(patchBytes, kHdrDeltaPatch);
    size_t patchFFIOff  = opt_header_offset(patchBytes, kHdrFileFormat);
    if (patchDescOff == kNotFound || patchFFIOff == kNotFound) fail("patch missing descriptors");
    if (be16(patchBytes, patchFFIOff + 6) != 3 /*DELTA*/) fail("patch is not delta-compressed");

    const uint8_t* pd = patchBytes + patchDescOff;       // delta patch descriptor
    uint32_t baseHeaderSize        = be32(xexBytes, 8);
    uint32_t sizeOfTargetHeaders   = be32(pd, 0x30);
    uint32_t deltaHeadersSourceOff = be32(pd, 0x34);
    uint32_t deltaHeadersSourceSz  = be32(pd, 0x38);
    uint32_t deltaHeadersTargetOff = be32(pd, 0x3C);
    uint32_t patchWindowSize = be32(patchBytes, patchFFIOff + 8);

    if (deltaHeadersSourceOff > baseHeaderSize) fail("incompatible (header source offset)");
    if (deltaHeadersSourceSz > baseHeaderSize - deltaHeadersSourceOff) fail("incompatible (header source size)");

    uint32_t headerTargetSize = sizeOfTargetHeaders;
    if (headerTargetSize == 0) headerTargetSize = deltaHeadersTargetOff + deltaHeadersSourceSz;

    // --- Build the new headers ---
    uint32_t newXexHeaderSize = std::max(headerTargetSize, baseHeaderSize);
    std::vector<uint8_t> out(newXexHeaderSize, 0);
    std::memcpy(out.data(), xexBytes, headerTargetSize);
    if (deltaHeadersSourceOff > 0)
        std::memmove(&out[deltaHeadersTargetOff], &out[deltaHeadersSourceOff], deltaHeadersSourceSz);

    lzx_delta_apply(pd + 0x4C, be32(pd, 0), patchWindowSize, out.data());

    out.resize(headerTargetSize);

    // --- Append the base data region after the new headers ---
    uint32_t newSecurityOff = be32(out.data(), 0x10);
    uint32_t newImageSize   = be32(out.data(), newSecurityOff + 4);
    // The base image (decompressed) may be larger than the patched image (a
    // shrinking patch), so the working buffer must fit the larger of the two.
    uint32_t baseImageSize0 = be32(xexBytes, be32(xexBytes, 0x10) + 4);
    size_t workSize = size_t(headerTargetSize) + std::max(newImageSize, baseImageSize0) + 0x1000;
    out.resize(workSize, 0);
    std::memset(&out[headerTargetSize], 0, out.size() - headerTargetSize);
    std::memcpy(&out[headerTargetSize], &xexBytes[baseHeaderSize], xexBytesSize - baseHeaderSize);

    // --- Derive & validate keys ---
    uint32_t baseSecurityOff  = be32(xexBytes, 0x10);
    uint32_t patchSecurityOff = be32(patchBytes, 0x10);
    // Retail and devkit XEXs wrap the title key with different master keys; try
    // both and keep the one whose derived image-key-source matches the base.
    static const uint8_t zeroKek[16] = {0};
    const uint8_t* keks[3] = { crypto::kRetailKey, zeroKek, crypto::kXex1Key };
    uint8_t origKey[16], patchKey[16];
    bool keyOk = false;
    for (int ki = 0; ki < 3 && !keyOk; ++ki) {
        uint8_t newKey[16], imgKeySrc[16];
        std::memcpy(origKey,   &xexBytes[baseSecurityOff + 0x150], 16);
        std::memcpy(newKey,    &out[newSecurityOff + 0x150], 16);
        std::memcpy(patchKey,  &patchBytes[patchSecurityOff + 0x150], 16);
        std::memcpy(imgKeySrc, pd + 0x20, 16);
        crypto::Aes128 kek(keks[ki]);
        kek.decrypt_block(origKey, origKey);
        kek.decrypt_block(newKey, newKey);
        crypto::Aes128 newCipher(newKey);
        newCipher.decrypt_block(patchKey, patchKey);
        newCipher.decrypt_block(imgKeySrc, imgKeySrc);
        keyOk = std::memcmp(imgKeySrc, origKey, 16) == 0;
    }
    if (!keyOk) fail("patch incompatible with this base xex");

    // --- Decrypt the base image region ---
    size_t baseFFIOff = opt_header_offset(xexBytes, kHdrFileFormat);
    if (baseFFIOff == kNotFound) fail("base missing file format info");
    uint16_t baseEnc  = be16(xexBytes, baseFFIOff + 4);
    uint16_t baseComp = be16(xexBytes, baseFFIOff + 6);
    if (baseEnc == 1) {
        crypto::Aes128 img(origKey);
        uint8_t iv[16] = {0};
        img.cbc_decrypt(&out[headerTargetSize], (xexBytesSize - baseHeaderSize) & ~size_t(15), iv);
    } else if (baseEnc != 0) {
        fail("unsupported base encryption");
    }

    // --- Decompress the base image in place ---
    uint32_t baseImageSize = be32(xexBytes, baseSecurityOff + 4);
    if (baseComp == 1) { // basic: reverse-iterate interleaved {data,zero} blocks
        size_t bi = baseFFIOff + 8;
        uint32_t infoSize = be32(xexBytes, baseFFIOff);
        int32_t numBlocks = int32_t(infoSize / 8) - 1;
        int32_t compSize = 0, imgSize = 0;
        for (int32_t i = 0; i < numBlocks; ++i) {
            compSize += be32(xexBytes, bi + i*8);
            imgSize  += be32(xexBytes, bi + i*8) + be32(xexBytes, bi + i*8 + 4);
        }
        uint8_t* src = out.data() + headerTargetSize + compSize;
        uint8_t* dstc = out.data() + headerTargetSize + imgSize;
        for (int32_t i = numBlocks - 1; i >= 0; --i) {
            uint32_t ds = be32(xexBytes, bi + i*8), zs = be32(xexBytes, bi + i*8 + 4);
            dstc -= zs; std::memset(dstc, 0, zs);
            dstc -= ds; src -= ds; std::memmove(dstc, src, ds);
        }
    } else if (baseComp == 2) { // normal LZX: de-frame then decode
        const uint8_t* exe = &out[headerTargetSize];
        uint32_t window = be32(xexBytes, baseFFIOff + 8);
        std::vector<uint8_t> stream;
        const uint8_t* p = exe;
        uint32_t blockSize = be32(xexBytes, baseFFIOff + 12); // first block size
        while (blockSize) {
            uint32_t next = be32(p, 0);
            const uint8_t* bp = p + 24;
            const uint8_t* blockEnd = p + blockSize;
            while (bp + 2 <= blockEnd) {
                uint32_t chunk = (uint32_t(bp[0]) << 8) | bp[1]; bp += 2;
                if (!chunk) break;
                stream.insert(stream.end(), bp, bp + chunk); bp += chunk;
            }
            p += blockSize; blockSize = next;
        }
        auto image = compress::lzx_decompress(stream.data(), stream.size(), baseImageSize, window);
        std::memcpy(&out[newXexHeaderSize], image.data(), image.size());
    } else if (baseComp != 0) {
        fail("unsupported base compression");
    }

    // The new headers now describe an uncompressed, unencrypted image.
    size_t newFFIOff = opt_header_offset(out.data(), kHdrFileFormat);
    if (newFFIOff != kNotFound) { wr16(&out[newFFIOff + 4], 0, 0); wr16(&out[newFFIOff + 6], 0, 0); }

    // --- Decrypt patch data, then apply the image delta blocks ---
    std::vector<uint8_t> patchData(patchBytes + be32(patchBytes, 8), patchBytes + patchBytesSize);
    uint16_t patchEnc = be16(patchBytes, patchFFIOff + 4);
    if (patchEnc == 1) {
        crypto::Aes128 pc(patchKey);
        uint8_t iv[16] = {0};
        pc.cbc_decrypt(patchData.data(), patchData.size() & ~size_t(15), iv);
    } else if (patchEnc != 0) {
        fail("unsupported patch encryption");
    }

    uint32_t newHeaderSize = be32(out.data(), 8);
    uint8_t* outExe = &out[newHeaderSize];
    uint32_t deltaImageSourceOff = be32(pd, 0x40);
    uint32_t deltaImageSourceSz  = be32(pd, 0x44);
    uint32_t deltaImageTargetOff = be32(pd, 0x48);
    if (deltaImageSourceOff > 0)
        std::memmove(&outExe[deltaImageTargetOff], &outExe[deltaImageSourceOff], deltaImageSourceSz);

    // The first block's {size, hash} live in the header; each block then begins
    // with the next block's {size, hash}.
    uint8_t* cursor = patchData.data();
    uint32_t curBlockSize = be32(patchBytes, patchFFIOff + 12);
    uint8_t  curHash[20];
    std::memcpy(curHash, patchBytes + patchFFIOff + 16, 20);
    PDBG("image patch: newImageSize=0x%X workSize=0x%zX outExeOff=0x%X patchData=0x%zX\n",
         newImageSize, out.size(), newHeaderSize, patchData.size());
    while (curBlockSize > 0) {
        PDBG("block size=0x%X (cursor off 0x%zX of 0x%zX)\n", curBlockSize,
             size_t(cursor - patchData.data()), patchData.size());
        auto h = crypto::Sha1::hash(cursor, curBlockSize);
        if (std::memcmp(h.data(), curHash, 20) != 0) fail("patch block hash mismatch");
        uint32_t nextBlockSize = be32(cursor, 0);
        uint8_t nextHash[20]; std::memcpy(nextHash, cursor + 4, 20);
        cursor += 24;
        lzx_delta_apply(cursor, curBlockSize - 24, patchWindowSize, outExe);
        cursor += curBlockSize - 24;
        curBlockSize = nextBlockSize;
        std::memcpy(curHash, nextHash, 20);
    }

    out.resize(size_t(headerTargetSize) + newImageSize); // trim working slack
    return out;
}

} // namespace xex
