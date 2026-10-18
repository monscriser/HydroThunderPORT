// convert.cpp - basefile format conversions.
#include "convert.h"
#include "basefile.h"
#include "crypto/aes.h"
#include "crypto/keys.h"
#include "crypto/sha1.h"
#include "crypto/xex_sign.h"
#include "compress/lzx.h"
#include <cstring>
#include <algorithm>

namespace xex {
namespace {
void put16(std::vector<uint8_t>& d, size_t o, uint16_t v) {
    d[o] = uint8_t(v >> 8); d[o+1] = uint8_t(v);
}
void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
    d[o]=uint8_t(v>>24); d[o+1]=uint8_t(v>>16); d[o+2]=uint8_t(v>>8); d[o+3]=uint8_t(v);
}
uint32_t get32(const std::vector<uint8_t>& d, size_t o) {
    return (uint32_t(d[o])<<24)|(uint32_t(d[o+1])<<16)|(uint32_t(d[o+2])<<8)|d[o+3];
}



} // namespace

std::vector<uint8_t> decompress_to_basic(const XexFile& x, bool binary,
                                         const std::vector<uint8_t>* image_in) {
    // Raw, decrypted, decompressed image (or a caller-supplied edited one).
    std::vector<uint8_t> image = image_in ? *image_in : reconstruct_basefile(x);

    // The "basic" format stores (data_size, zero_size) runs; zero runs are not
    // written to the file. -c u segments the image so zero regions are elided;
    // xextool decides the runs on a 32 KB granule: a granule that is entirely
    // zero belongs to a zero run. (Verified exactly against xextool's own output
    // on the 36 MB CoD image.) -c b instead emits a single run covering
    // everything, storing the image flat.
    const size_t G = 0x8000;
    std::vector<std::pair<uint32_t, uint32_t>> runs;   // (data_size, zero_size)
    if (binary) {
        runs.emplace_back(uint32_t(image.size()), 0u);
    } else {
        const size_t ng = image.size() / G;
        auto zero_granule = [&](size_t g) {
            const uint8_t* p = image.data() + g * G;
            for (size_t k = 0; k < G; ++k) if (p[k]) return false;
            return true;
        };
        for (size_t g = 0; g < ng;) {
            size_t d = 0, z = 0;
            while (g < ng && !zero_granule(g)) { ++d; ++g; }
            while (g < ng &&  zero_granule(g)) { ++z; ++g; }
            if (d || z) runs.emplace_back(uint32_t(d * G), uint32_t(z * G));
        }
        if (runs.empty()) runs.emplace_back(uint32_t(image.size()), 0u);
        // A tail below the granule size is data. It can only be folded into the
        // last run when that run has no zero part -- otherwise the tail would sit
        // AFTER those zeros and needs its own pair. (CoD's image is an exact
        // multiple of 32 KB so it never showed this; xbdm's 0xAF000 does:
        // xextool emits (0xA0000,0x8000) then (0x7000,0).)
        size_t tail = image.size() - ng * G;
        if (tail) {
            if (runs.back().second == 0) runs.back().first += uint32_t(tail);
            else                         runs.emplace_back(uint32_t(tail), 0u);
        }
    }

    // Stored payload = the data runs only, concatenated.
    std::vector<uint8_t> data;
    size_t pos = 0;
    for (auto& r : runs) {
        data.insert(data.end(), image.begin() + pos, image.begin() + pos + r.first);
        pos += size_t(r.first) + r.second;
    }

    // Encryption is applied to the stored payload as a single CBC stream.
    uint16_t enc = x.encryption_type().value_or(kEncNone);
    if (enc != kEncNone) {
        uint8_t title_key[16];
        crypto::Aes128 master(detect_kek(x));   // devkit files wrap with a zero KEK
        master.decrypt_block(x.security().aes_key.data(), title_key);
        uint8_t iv[16] = {0};
        crypto::Aes128 cipher(title_key);
        cipher.cbc_encrypt(data.data(), data.size() & ~size_t(15), iv);
    }

    // The stored payload is zero-padded up to a whole 32 KB granule. The padding
    // goes on AFTER encryption -- it stays plain zeros even in an encrypted file.
    // Only xbdm shows this (0xA7000 -> 0xA8000); every other sample's payload is
    // already granule-aligned, which is why it went unnoticed.
    data.resize((data.size() + (G - 1)) & ~(G - 1), 0);

    // Rebuild the header with a file-format-info describing the runs.
    std::vector<HeaderEntry> ents = header_entries(x);
    for (HeaderEntry& e : ents) {
        if (e.key != kHdrFileFormatInfo) continue;
        e.body.assign(8 + runs.size() * 8, 0);
        put32(e.body, 0, uint32_t(e.body.size()));
        put16(e.body, 4, enc);
        put16(e.body, 6, kCompBasic);
        for (size_t r = 0; r < runs.size(); ++r) {
            put32(e.body, 8 + r * 8 + 0, runs[r].first);
            put32(e.body, 8 + r * 8 + 4, runs[r].second);
        }
        e.is_blob = true;
    }
    HeaderBuild B = build_header(x, std::move(ents));
    if (!B.ok) return x.raw();

    crypto::devkit_sign(B.hdr, B.data_offset,
                        crypto::verify_devkit_sig(&x.raw()[x.sec_info_offset() + 8]), B.sec);

    B.hdr.insert(B.hdr.end(), data.begin(), data.end());
    return B.hdr;
}

// Inverse of decompress_to_basic: LZX-compress the raw image into the XEX
// "normal" (block-framed) format. Byte-identical to xextool's -c c for the
// data region + file-format-info; the RSA header signature and the security
// info image hash are NOT recomputed here (see notes in main).
std::vector<uint8_t> compress_to_normal(const XexFile& x, bool encrypt,
                                        const std::vector<uint8_t>* image_in) {
    std::vector<uint8_t> image = image_in ? *image_in : reconstruct_basefile(x);

    // 1. LZX-compress -> chunk-framed stream (window 0x8000, no E8 for games).
    std::vector<uint8_t> stream =
        compress::lzx_compress(image.data(), image.size(), 0x8000, 0);

    // 2. Pack chunks into storage blocks. A block holds whole chunks until the
    //    next won't fit in 0x10000-24 bytes; block size = roundup(used+24,0x800).
    const size_t CAP = 0x10000 - 24;
    std::vector<std::vector<uint8_t>> contents;
    std::vector<uint8_t> cur;
    for (size_t o = 0; o + 2 <= stream.size();) {
        uint32_t clen = (uint32_t(stream[o]) << 8) | stream[o + 1];
        size_t whole = 2 + clen;
        if (!cur.empty() && cur.size() + whole > CAP) { contents.push_back(std::move(cur)); cur.clear(); }
        cur.insert(cur.end(), stream.begin() + o, stream.begin() + o + whole);
        o += whole;
    }
    if (!cur.empty()) contents.push_back(std::move(cur));

    // 3. Build blocks back-to-front, chaining SHA-1(next block) into each header.
    size_t nb = contents.size();
    std::vector<std::vector<uint8_t>> blocks(nb);
    uint32_t next_size = 0;
    uint8_t next_hash[20] = {0};
    for (size_t i = nb; i-- > 0;) {
        size_t used = contents[i].size();
        size_t bsize = (used + 24 + 0x7ff) & ~size_t(0x7ff);
        std::vector<uint8_t> blk(bsize, 0);
        blk[0] = uint8_t(next_size >> 24); blk[1] = uint8_t(next_size >> 16);
        blk[2] = uint8_t(next_size >> 8);  blk[3] = uint8_t(next_size);
        std::memcpy(&blk[4], next_hash, 20);
        std::memcpy(&blk[24], contents[i].data(), used);
        next_size = uint32_t(bsize);
        auto h = crypto::Sha1::hash(blk.data(), blk.size());
        std::memcpy(next_hash, h.data(), 20);
        blocks[i] = std::move(blk);
    }
    uint32_t first_block_size = nb ? uint32_t(blocks[0].size()) : 0;
    std::array<uint8_t, 20> first_hash{};
    if (nb) first_hash = crypto::Sha1::hash(blocks[0].data(), blocks[0].size());

    // 4. Optional AES-CBC encryption of the compressed data (whole stream).
    std::vector<uint8_t> data;
    for (auto& blk : blocks) data.insert(data.end(), blk.begin(), blk.end());
    if (encrypt) {
        uint8_t title_key[16];
        crypto::Aes128 master(detect_kek(x));   // devkit files wrap with a zero KEK
        master.decrypt_block(x.security().aes_key.data(), title_key);
        uint8_t iv[16] = {0};
        crypto::Aes128 cipher(title_key);
        cipher.cbc_encrypt(data.data(), data.size() & ~size_t(15), iv);
    }

    // 5. Rebuild the header with a 0x24-byte file-format-info describing the new
    //    normal-compressed stream.
    std::vector<HeaderEntry> ents = header_entries(x);
    for (HeaderEntry& e : ents) {
        if (e.key != kHdrFileFormatInfo) continue;
        e.body.assign(0x24, 0);
        put32(e.body, 0, 0x24);
        put16(e.body, 4, encrypt ? kEncAes : kEncNone);
        put16(e.body, 6, kCompNormal);
        put32(e.body, 8, 0x8000);                  // LZX window
        put32(e.body, 12, first_block_size);
        std::memcpy(&e.body[16], first_hash.data(), 20);
        e.is_blob = true;
    }
    // -c c does NOT repack: xextool rewrites the FFI in place and leaves every
    // following blob where it was (hence CoD's 0xC gap after a 0x30->0x24 shrink).
    // The exception is when the new 0x24 FFI would OVERLAP the next blob (a source
    // whose FFI was smaller, e.g. a -c b output with a 0x10 FFI): then the tail is
    // relaid from round_up(ffi_end, 0x10) -- the same 0x10 padding -a uses, and
    // unlike -c u/-c b which pack at exact size.
    uint32_t ffi_off = 0;
    for (const HeaderEntry& e : ents)
        if (e.key == kHdrFileFormatInfo) ffi_off = e.val;
    if (ffi_off) {
        const uint32_t ffi_end = ffi_off + 0x24;
        bool overlap = false;
        for (const HeaderEntry& e : ents)
            if (e.is_blob && e.key != kHdrImportLibraries &&
                e.val > ffi_off && e.val < ffi_end) overlap = true;
        if (overlap) {
            std::vector<HeaderEntry*> tail;
            for (HeaderEntry& e : ents)
                if (e.is_blob && e.key != kHdrImportLibraries && e.val > ffi_off)
                    tail.push_back(&e);
            std::sort(tail.begin(), tail.end(),
                      [](const HeaderEntry* a, const HeaderEntry* b) { return a->val < b->val; });
            uint32_t pos = (ffi_end + 0xF) & ~0xFu;
            for (HeaderEntry* e : tail) { e->val = pos; pos += uint32_t(e->body.size()); }
        }
    }
    HeaderBuild B = build_header(x, std::move(ents), /*repack=*/false);
    if (!B.ok) return x.raw();

    // 6. HeaderHash + signature. Sign with the devkit key only when the source
    //    was devkit-signed; retail output gets a zeroed signature, as xextool
    //    does. The security info is NOT always at 0x120 (it tracks the entry
    //    count: 0xF8 for xam's 12) -- derive it, never assume.
    bool devkit = crypto::verify_devkit_sig(&B.hdr[B.sec + 8]);
    crypto::devkit_sign(B.hdr, B.data_offset, devkit, B.sec);

    B.hdr.insert(B.hdr.end(), data.begin(), data.end());
    return B.hdr;
}

std::vector<HeaderEntry> header_entries(const XexFile& x) {
    const std::vector<uint8_t>& src = x.raw();
    const uint32_t data_off = x.data_offset();
    std::vector<HeaderEntry> ents;
    uint32_t oc = get32(src, 0x14);
    for (uint32_t j = 0; j < oc; ++j) {
        size_t e = 0x18 + size_t(j) * 8;
        HeaderEntry en;
        en.key = get32(src, e);
        en.val = get32(src, e + 4);
        uint32_t lo = en.key & 0xFF;
        // Key low byte: 0 => the value is inline data, not an offset; 0xFF => the
        // blob leads with its own u32 size; otherwise it is that many u32s.
        if (lo != 0 && en.val != 0 && en.val < data_off) {
            uint32_t sz = (lo == 0xFF) ? get32(src, en.val) : lo * 4;
            if (sz && size_t(en.val) + sz <= data_off) {
                en.is_blob = true;
                en.body.assign(src.begin() + en.val, src.begin() + en.val + sz);
            }
        }
        ents.push_back(std::move(en));
    }
    return ents;
}

HeaderBuild build_header(const XexFile& x, std::vector<HeaderEntry> ents, bool repack) {
    HeaderBuild B;
    const std::vector<uint8_t>& src = x.raw();
    const uint32_t old_sec = get32(src, 0x10);
    if (size_t(old_sec) + 4 > src.size()) return B;
    const uint32_t secsz = get32(src, old_sec);
    if (!secsz || size_t(old_sec) + secsz > src.size()) return B;

    std::sort(ents.begin(), ents.end(),
              [](const HeaderEntry& a, const HeaderEntry& b) { return a.key < b.key; });

    const uint32_t count = uint32_t(ents.size());
    const uint32_t sec   = 0x18 + count * 8 + 0x80;   // always a 0x80 zero gap

    // Blobs go in key order, packed at their exact size, starting just past the
    // security info -- except the import-libraries blob, which sits flush against
    // data_offset. data_offset then falls out of where the rest ended.
    uint32_t import_size = 0;
    for (const HeaderEntry& e : ents)
        if (e.key == kHdrImportLibraries && e.is_blob) import_size = uint32_t(e.body.size());

    uint32_t pos = sec + secsz;
    for (HeaderEntry& e : ents) {
        if (!e.is_blob || e.key == kHdrImportLibraries) continue;
        if (repack) e.val = pos;                       // contiguous, key order
        pos = std::max(pos, e.val + uint32_t(e.body.size()));
    }
    const uint32_t data_offset = (pos + import_size + 0xFFFu) & ~0xFFFu;
    const uint32_t import_off  = data_offset - import_size;
    for (HeaderEntry& e : ents)
        if (e.key == kHdrImportLibraries && e.is_blob) e.val = import_off;

    B.hdr.assign(data_offset, 0);
    std::copy(src.begin(), src.begin() + 0x18, B.hdr.begin());
    put32(B.hdr, 0x08, data_offset);
    put32(B.hdr, 0x10, sec);
    put32(B.hdr, 0x14, count);
    std::copy(src.begin() + old_sec, src.begin() + old_sec + secsz, B.hdr.begin() + sec);
    for (uint32_t j = 0; j < count; ++j) {
        size_t e = 0x18 + size_t(j) * 8;
        put32(B.hdr, e + 0, ents[j].key);
        put32(B.hdr, e + 4, ents[j].val);
        if (ents[j].is_blob)
            std::copy(ents[j].body.begin(), ents[j].body.end(), B.hdr.begin() + ents[j].val);
    }
    B.data_offset = data_offset;
    B.sec = sec;
    B.ok = true;
    return B;
}

std::vector<uint8_t> convert_machine(const XexFile& x, bool to_devkit) {
    // -m RE-SERIALIZES the basefile through the same rebuild path as -c, then
    // re-keys and re-signs. It is NOT a header-only transform: on an
    // MS-compressed file like xam.xex, `-m d` is byte-identical to `-c c` (both
    // sha a47ff3b1) because xextool decompresses and recompresses with its own
    // encoder. It only LOOKS like a verbatim no-op on files that are already in
    // xextool's exact output form (e.g. DashLaunch). The compression format is
    // preserved: compressed stays compressed, basic stays basic.
    const bool encrypted = x.is_encrypted();
    std::vector<uint8_t> out = x.is_compressed()
                             ? compress_to_normal(x, encrypted)
                             : decompress_to_basic(x, false);
    if (out.size() < 0x18) return x.raw();

    const uint32_t data_off = get32(out, 8);
    const uint32_t sec      = get32(out, 0x10);
    if (size_t(sec) + 8 + 256 + 0x58 > out.size()) return x.raw();

    // Key rewrap: title = dec(source KEK, stored); restored = enc(target, title).
    // The title key itself is unchanged, so the ciphertext just written by the
    // rebuild stays valid -- only the wrapping changes to the target's KEK.
    if (encrypted) {
        uint8_t title[16], wrapped[16], zero[16] = {0};
        crypto::Aes128(detect_kek(x)).decrypt_block(x.security().aes_key.data(), title);
        crypto::Aes128(to_devkit ? zero : crypto::kRetailKey).encrypt_block(title, wrapped);
        std::memcpy(&out[sec + 8 + 256 + 0x48], wrapped, 16);          // image-info +0x48
    }

    // HeaderHash always; signature only for devkit (no retail private key).
    crypto::devkit_sign(out, data_off, to_devkit, sec);
    return out;
}

std::vector<uint8_t> set_encryption(const XexFile& x, bool encrypt) {
    const std::vector<uint8_t>& src = x.raw();
    if (encrypt == x.is_encrypted()) return src;   // already in target state

    const uint32_t data_off = x.data_offset();

    // Title key = AES-ECB-decrypt(stored aes_key, master KEK). The KEK is
    // detected per-file: devkit XEXs wrap the title key under a zero KEK.
    uint8_t title_key[16];
    crypto::Aes128 master(detect_kek(x));
    master.decrypt_block(x.security().aes_key.data(), title_key);
    crypto::Aes128 cipher(title_key);

    std::vector<uint8_t> data(src.begin() + data_off, src.end());
    uint8_t iv[16] = {0};
    size_t n = data.size() & ~size_t(15);
    if (encrypt) cipher.cbc_encrypt(data.data(), n, iv);
    else         cipher.cbc_decrypt(data.data(), n, iv);

    // xextool rebuilds the header on every write, then re-signs. Only the
    // file-format-info's encryption field changes here; its size is unaffected.
    std::vector<HeaderEntry> ents = header_entries(x);
    for (HeaderEntry& e : ents)
        if (e.key == kHdrFileFormatInfo && e.body.size() >= 6)
            put16(e.body, 4, encrypt ? kEncAes : kEncNone);
    HeaderBuild B = build_header(x, std::move(ents), /*repack=*/false);
    if (!B.ok) return src;

    crypto::devkit_sign(B.hdr, B.data_offset,
                        crypto::verify_devkit_sig(&src[x.sec_info_offset() + 8]), B.sec);
    B.hdr.insert(B.hdr.end(), data.begin(), data.end());
    return B.hdr;
}

std::vector<uint8_t> reserialize_with_image(const XexFile& x,
                                            const std::vector<uint8_t>& image) {
    return x.is_compressed() ? compress_to_normal(x, x.is_encrypted(), &image)
                             : decompress_to_basic(x, false, &image);
}

} // namespace xex
