// basefile.cpp - decrypt + decompress a XEX basefile into the raw PE image.
#include "basefile.h"
#include "compress/lzx.h"
#include "crypto/aes.h"
#include "crypto/keys.h"
#include "crypto/sha1.h"
#include "crypto/xex_sign.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace xex {

namespace {
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | p[3];
}
} // namespace

// Reconstruct using a specific key-encryption-key (retail vs devkit) to unwrap
// the title key. kek is ignored when the basefile is not encrypted.
static std::vector<uint8_t> reconstruct_with_kek(const XexFile& x, const uint8_t kek[16]) {
    const auto& raw = x.raw();
    if (x.data_offset() > raw.size())
        throw std::runtime_error("basefile: bad data offset");

    // Working copy of the basefile data region.
    std::vector<uint8_t> work(raw.begin() + x.data_offset(), raw.end());

    // 1. Decrypt (AES-128-CBC, zero IV) using the title key unwrapped from the
    //    security info with the given master key.
    if (x.is_encrypted()) {
        uint8_t title_key[16];
        crypto::Aes128 master(kek);
        master.decrypt_block(x.security().aes_key.data(), title_key);
        uint8_t iv[16] = {0};
        crypto::Aes128 cipher(title_key);
        size_t n = work.size() & ~size_t(15);
        cipher.cbc_decrypt(work.data(), n, iv);
    }

    const uint32_t image_size = x.security().image_size;
    uint16_t comp = x.compression_type().value_or(kCompNone);

    // 2a. Normal (LZX) compression: strip block/chunk framing, then LZX-decode.
    if (comp == kCompNormal) {
        std::vector<uint8_t> stream;
        stream.reserve(work.size());
        const uint8_t* p   = work.data();
        const uint8_t* end = work.data() + work.size();
        uint32_t block_size = x.comp_first_block_size();
        while (block_size) {
            if (p + block_size > end || block_size < 24)
                throw std::runtime_error("basefile: truncated compression block");
            uint32_t next_size = be32(p);          // next block descriptor
            const uint8_t* bp = p + 24;            // skip size(4) + hash(20)
            const uint8_t* block_end = p + block_size;
            while (bp + 2 <= block_end) {
                uint32_t chunk = (uint32_t(bp[0]) << 8) | bp[1];
                bp += 2;
                if (chunk == 0) break;
                if (bp + chunk > block_end)
                    throw std::runtime_error("basefile: bad chunk size");
                stream.insert(stream.end(), bp, bp + chunk);
                bp += chunk;
            }
            p += block_size;
            block_size = next_size;
        }
        return compress::lzx_decompress(stream.data(), stream.size(),
                                        image_size, x.comp_window_size());
    }

    // 2b. Basic format: the file-format-info holds (data_size, zero_size) pairs
    //     that interleave stored data with zero-filled gaps (.bss / heap). Walk
    //     them, copying data from the decrypted region and inserting zero runs.
    const OptionalHeader* ff = x.find_header(kHdrFileFormatInfo);
    if (comp == kCompNone && ff) {
        Reader hdr(x.raw());
        size_t o = ff->value;
        uint32_t info_size = hdr.u32(o);
        uint32_t nblocks = info_size >= 8 ? (info_size - 8) / 8 : 0;
        std::vector<uint8_t> image;
        image.reserve(image_size);
        size_t pos = 0;
        for (uint32_t i = 0; i < nblocks; ++i) {
            uint32_t data_size = hdr.u32(o + 8 + size_t(i) * 8);
            uint32_t zero_size = hdr.u32(o + 8 + size_t(i) * 8 + 4);
            if (pos + data_size > work.size()) data_size = uint32_t(work.size() - pos);
            image.insert(image.end(), work.begin() + pos, work.begin() + pos + data_size);
            pos += data_size;
            image.insert(image.end(), zero_size, 0);
        }
        if (image.size() < image_size) image.resize(image_size, 0);
        return image;
    }

    work.resize(image_size, 0);
    return work;
}

std::vector<uint8_t> reconstruct_basefile(const XexFile& x) {
    if (!x.is_encrypted())
        return reconstruct_with_kek(x, crypto::kRetailKey);

    // Retail and devkit XEXs wrap the title key with different master keys.
    // Try the retail key; if the result isn't a valid PE, fall back to the
    // devkit key (all zeros). A wrong key yields garbage that usually fails to
    // decompress, so each attempt is guarded. A PE basefile starts with "MZ".
    auto is_pe = [](const std::vector<uint8_t>& b) {
        return b.size() >= 2 && b[0] == 'M' && b[1] == 'Z';
    };
    auto try_kek = [&](const uint8_t* kek) -> std::vector<uint8_t> {
        try { return reconstruct_with_kek(x, kek); }
        catch (...) { return {}; }
    };

    static const uint8_t kDevkitKey[16] = {0};
    const uint8_t* keks[3] = { crypto::kRetailKey, kDevkitKey, crypto::kXex1Key };
    std::vector<uint8_t> first;
    for (const uint8_t* kek : keks) {
        std::vector<uint8_t> img = try_kek(kek);
        if (is_pe(img)) return img;
        if (first.empty() && !img.empty()) first = std::move(img);
    }
    if (!first.empty()) return first;          // non-PE basefile (e.g. data xex)
    return reconstruct_with_kek(x, crypto::kRetailKey); // all failed: surface error
}

const uint8_t* detect_kek(const XexFile& x) {
    static const uint8_t kDevkitKey[16] = {0};
    // Unencrypted: there is no ciphertext to trial-decrypt, so pick by machine
    // type instead. This matters when ENCRYPTING such a file (-e e), where the
    // title key must be unwrapped with the KEK the output will be keyed under.
    if (!x.is_encrypted())
        return is_retail_xex(x) ? crypto::kRetailKey : kDevkitKey;
    const uint8_t* keks[3] = { crypto::kRetailKey, kDevkitKey, crypto::kXex1Key };
    const auto& raw = x.raw();
    const size_t off = x.data_offset();
    uint16_t comp = x.compression_type().value_or(kCompNone);
    for (const uint8_t* kek : keks) {
        uint8_t title_key[16];
        crypto::Aes128 master(kek);
        master.decrypt_block(x.security().aes_key.data(), title_key);
        crypto::Aes128 cipher(title_key);
        uint8_t iv[16] = {0};
        if (comp == kCompNormal) {
            uint32_t fbs = x.comp_first_block_size();
            if (fbs == 0 || off + fbs > raw.size()) continue;
            std::vector<uint8_t> block(raw.begin() + off, raw.begin() + off + fbs);
            cipher.cbc_decrypt(block.data(), block.size() & ~size_t(15), iv);
            auto h = crypto::Sha1::hash(block.data(), block.size());
            const OptionalHeader* ff = x.find_header(kHdrFileFormatInfo);
            if (!ff) continue;
            Reader r(raw);
            std::vector<uint8_t> stored = r.bytes(ff->value + 16, 20);
            if (std::equal(h.begin(), h.end(), stored.begin())) return kek;
        } else {
            if (off + 16 > raw.size()) continue;
            uint8_t head[16];
            std::copy(raw.begin() + off, raw.begin() + off + 16, head);
            cipher.cbc_decrypt(head, 16, iv);
            if (head[0] == 'M' && head[1] == 'Z') return kek;
        }
    }
    return crypto::kRetailKey;
}

bool is_retail_xex(const XexFile& x) {
    // A verified devkit RSA signature is the definitive machine-type test, and
    // unlike the KEK heuristic it works for unencrypted files too (e.g. devkit
    // homebrew like DashLaunch, which the old "unencrypted => retail" rule got
    // wrong). Only fall back to the KEK trial-decrypt when it doesn't verify.
    const auto& sraw = x.raw();
    if (sraw.size() >= 0x14) {
        uint32_t sec = be32(&sraw[0x10]);          // security-info offset
        if (size_t(sec) + 8 + 256 <= sraw.size() &&
            crypto::verify_devkit_sig(&sraw[sec + 8]))
            return false;                          // verified devkit signature
    }

    if (!x.is_encrypted()) return true;        // retail / MS-signed

    uint8_t title_key[16];
    crypto::Aes128 master(crypto::kRetailKey);
    master.decrypt_block(x.security().aes_key.data(), title_key);
    crypto::Aes128 cipher(title_key);

    const auto& raw = x.raw();
    const size_t off = x.data_offset();
    uint8_t iv[16] = {0};

    if (x.compression_type().value_or(kCompNone) == kCompNormal) {
        // Decrypt the first block and compare to its stored integrity hash.
        uint32_t fbs = x.comp_first_block_size();
        if (fbs == 0 || off + fbs > raw.size()) return true;
        std::vector<uint8_t> block(raw.begin() + off, raw.begin() + off + fbs);
        cipher.cbc_decrypt(block.data(), block.size() & ~size_t(15), iv);
        auto h = crypto::Sha1::hash(block.data(), block.size());
        const OptionalHeader* ff = x.find_header(kHdrFileFormatInfo);
        if (!ff) return true;
        Reader r(raw);
        std::vector<uint8_t> stored = r.bytes(ff->value + 16, 20);
        return std::equal(h.begin(), h.end(), stored.begin());
    }
    // Uncompressed: the decrypted image starts with the PE "MZ" magic.
    if (off + 16 > raw.size()) return true;
    uint8_t head[16];
    std::copy(raw.begin() + off, raw.begin() + off + 16, head);
    cipher.cbc_decrypt(head, 16, iv);
    return head[0] == 'M' && head[1] == 'Z';
}

} // namespace xex
