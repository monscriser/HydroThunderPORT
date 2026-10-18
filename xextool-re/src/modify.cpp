// modify.cpp - in-place XEX header modifications.
#include "modify.h"
#include "convert.h"
#include "crypto/xex_sign.h"
#include "crypto/sha1.h"
#include <cstring>
#include <algorithm>
#include <array>

namespace xex {
namespace {

void put32(std::vector<uint8_t>& d, size_t o, uint32_t v) {
    d[o]   = uint8_t(v >> 24); d[o+1] = uint8_t(v >> 16);
    d[o+2] = uint8_t(v >> 8);  d[o+3] = uint8_t(v);
}
uint32_t get32(const std::vector<uint8_t>& d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o+1]) << 16) |
           (uint32_t(d[o+2]) << 8) | d[o+3];
}


// -r l: drop each import library's minimum-version requirement, then rebuild the
// import digest chain. Operates on the import-libraries blob body. The chain runs
// BACK-TO-FRONT, exactly like the compressed block chain: the last library's
// nextImportDigest is zero, each earlier library stores SHA-1 of the NEXT
// library's block (skipping that block's 4-byte size field), and the caller
// stores SHA-1 of the FIRST library's block the same way into
// ImageInfo.ImportDigest. Rebuilding back-to-front matters: a library's own
// digest field lies inside the range its predecessor hashes.
bool strip_min_library_versions(std::vector<uint8_t>& body,
                                std::array<uint8_t, 20>& head_digest) {
    if (body.size() < 12) return false;
    uint32_t str_size = get32(body, 4);
    uint32_t count    = get32(body, 8);

    std::vector<std::pair<size_t, uint32_t>> libs;
    size_t p = 12 + size_t(str_size);
    for (uint32_t i = 0; i < count && p + 4 <= body.size(); ++i) {
        uint32_t bsz = get32(body, p);
        if (bsz < 0x28 || p + bsz > body.size()) return false;   // malformed
        libs.emplace_back(p, bsz);
        p += bsz;
    }
    if (libs.empty()) return false;

    // Block layout: size(4) nextImportDigest(20) id(4) version(4) minVersion(4)
    // name(2) count(2). Zero minVersion's build+QFE, keep major/minor; `version`
    // at +0x1C is left alone.
    for (auto& lb : libs)
        put32(body, lb.first + 0x20, get32(body, lb.first + 0x20) & 0xFF000000u);

    for (size_t i = libs.size(); i-- > 0;) {
        size_t off = libs[i].first;
        if (i + 1 < libs.size()) {
            auto h = crypto::Sha1::hash(&body[libs[i + 1].first + 4], libs[i + 1].second - 4);
            std::memcpy(&body[off + 4], h.data(), 20);        // nextImportDigest
        } else {
            std::memset(&body[off + 4], 0, 20);               // last: no next
        }
    }
    head_digest = crypto::Sha1::hash(&body[libs[0].first + 4], libs[0].second - 4);
    return true;
}

} // namespace

std::vector<uint8_t> add_bounding_path(const XexFile& x, const std::string& path) {
    const std::vector<uint8_t>& src = x.raw();
    const uint32_t data_off  = x.data_offset();
    const uint32_t old_sec   = get32(src, 0x10);
    const uint32_t old_count = get32(src, 0x14);
    const uint32_t sec_size  = get32(src, old_sec);
    if (x.find_header(kHdrBoundingPath)) return src;      // already present

    // NB this does NOT use build_header: -a is a distinct path in xextool that
    // pads the file-format-info's END up to a 0x10 boundary (in.xex: FFI at
    // 0x3788 + 0x30 = 0x37B8 -> next blob at 0x37C0), whereas the -c paths pack
    // at exact size (xam: FFI 0x1050 + 0x24 -> next at 0x1074). It also keeps
    // the source's data_offset rather than deriving it, so a file with a wasted
    // page (xbdm) is untested here.
    const uint32_t new_count = old_count + 1;
    const uint32_t new_sec   = 0x18 + new_count * 8 + 0x80;
    if (new_sec + sec_size > data_off) return src;

    const OptionalHeader* imp = x.find_header(kHdrImportLibraries);
    const uint32_t import_off = imp ? imp->value : data_off;

    // Bounding-path blob: [u32 size][path][NUL], size rounded up to 4.
    std::vector<uint8_t> bp((4 + path.size() + 1 + 3) & ~size_t(3), 0);
    put32(bp, 0, uint32_t(bp.size()));
    std::memcpy(&bp[4], path.data(), path.size());

    struct Ent { uint32_t key, val; std::vector<uint8_t> body; bool is_blob; };
    std::vector<Ent> ents;
    for (uint32_t j = 0; j < old_count; ++j) {
        size_t e = 0x18 + size_t(j) * 8;
        uint32_t key = get32(src, e), val = get32(src, e + 4);
        bool is_blob = (key & 0xFF) != 0 && val != 0 && val < data_off &&
                       val != import_off;
        std::vector<uint8_t> body;
        if (is_blob) {
            uint32_t lo = key & 0xFF;
            uint32_t sz = (lo == 0xFF) ? get32(src, val) : lo * 4;
            if (val + sz > data_off) return src;           // malformed
            body.assign(src.begin() + val, src.begin() + val + sz);
        }
        ents.push_back({key, val, std::move(body), is_blob});
    }
    ents.push_back({kHdrBoundingPath, 0, bp, true});
    std::sort(ents.begin(), ents.end(),
              [](const Ent& a, const Ent& b) { return a.key < b.key; });

    std::vector<uint8_t> out(data_off, 0);
    std::copy(src.begin(), src.begin() + 0x18, out.begin());
    put32(out, 0x10, new_sec);
    put32(out, 0x14, new_count);
    std::copy(src.begin() + old_sec, src.begin() + old_sec + sec_size,
              out.begin() + new_sec);
    std::copy(src.begin() + import_off, src.begin() + data_off,
              out.begin() + import_off);

    uint32_t pos = new_sec + sec_size;
    for (size_t j = 0; j < ents.size(); ++j) {
        Ent& en = ents[j];
        uint32_t val = en.val;
        if (en.is_blob) {
            if (pos + en.body.size() > import_off) return src;   // no room
            std::copy(en.body.begin(), en.body.end(), out.begin() + pos);
            val = pos;
            pos += uint32_t(en.body.size());
            if (en.key == kHdrFileFormatInfo) pos = (pos + 0xF) & ~0xFu;
        }
        size_t e = 0x18 + j * 8;
        put32(out, e + 0, en.key);
        put32(out, e + 4, val);
    }

    crypto::devkit_sign(out, data_off,
                        crypto::verify_devkit_sig(&src[old_sec + 8]), new_sec);
    out.insert(out.end(), src.begin() + data_off, src.end());
    return out;
}

std::vector<uint8_t> remove_limitations(const XexFile& x, const std::string& opts) {
    // -r RE-SERIALIZES the basefile like -m/-c before editing: `-r a` on xam
    // recompresses it (3190784 -> 3368960). It only looks like a pure header edit
    // on files already in xextool's output form (in.xex re-serializes to itself).
    std::vector<uint8_t> ser = x.is_compressed()
                             ? compress_to_normal(x, x.is_encrypted())
                             : decompress_to_basic(x, false);
    XexFile y;
    try { y = XexFile::parse(ser); }
    catch (const std::exception&) { return x.raw(); }

    const std::vector<uint8_t>& src = y.raw();
    const bool all = opts.find('a') != std::string::npos;
    auto has = [&](char c) { return all || opts.find(c) != std::string::npos; };

    // Directory-level edits first: dropping an entry or rewriting the import
    // blob changes the layout, so they have to happen before the rebuild.
    std::vector<HeaderEntry> ents = header_entries(y);
    bool dirty = false, dropped = false;
    std::array<uint8_t, 20> import_digest{};
    bool have_import_digest = false;

    auto drop = [&](uint32_t key) {
        auto it = std::remove_if(ents.begin(), ents.end(),
                                 [&](const HeaderEntry& e) { return e.key == key; });
        if (it != ents.end()) { ents.erase(it, ents.end()); dirty = dropped = true; }
    };
    if (has('b')) drop(kHdrBoundingPath);
    if (has('d')) drop(0x00008105);                       // bounding device id
    if (has('l')) {
        for (HeaderEntry& e : ents) {
            if (e.key != kHdrImportLibraries || !e.is_blob) continue;
            std::vector<uint8_t> before = e.body;
            if (strip_min_library_versions(e.body, import_digest)) {
                have_import_digest = true;
                if (e.body != before) dirty = true;
            }
        }
    }
    // NOTE: i/y/v/k/c (console/date/keyvault/revocation limits) are unmapped --
    // none of the available samples carry those limits, so there is nothing to
    // verify a guess against; they are left untouched rather than written wrong.

    // Repack ONLY when an entry was actually dropped ('b'/'d'): that moves the
    // security info (xam: 12 entries -> 11, sec 0xF8 -> 0xF0) and removes a blob,
    // so the old offsets no longer describe the layout. When nothing is dropped
    // the layout must be preserved verbatim -- cc1 carries a 0xC gap after its
    // FFI, and repacking would close it and diverge from xextool.
    HeaderBuild B = build_header(y, ents, /*repack=*/dropped);
    if (!B.ok) return x.raw();
    const size_t img = B.sec + 8 + 256;                   // image-info

    // Field-level edits land in the (rebuilt) security info.
    auto set32 = [&](size_t o, uint32_t v) {
        if (get32(B.hdr, o) != v) { put32(B.hdr, o, v); dirty = true; }
    };
    if (has('m')) {                                       // all media types
        set32(img + 4, get32(B.hdr, img + 4) & ~0x08u);   // ImageFlags: media bit
        set32(img + 116, 0xFFFFFFFF);                     // AllowedMediaTypes
    }
    if (has('r')) set32(img + 112, 0xFFFFFFFF);           // GameRegion: all regions
    if (has('z')) {                                       // zero MediaID
        for (size_t k = 0; k < 16; ++k)
            if (B.hdr[img + 56 + k]) { B.hdr[img + 56 + k] = 0; dirty = true; }
    }
    if (have_import_digest)
        std::memcpy(&B.hdr[img + 0x24], import_digest.data(), 20);

    if (!dirty) return x.raw();                           // nothing stripped

    // Re-sign: devkit input gets a real devkit signature, retail gets a zeroed
    // one (xextool has no retail private key). HeaderHash is always rebuilt.
    crypto::devkit_sign(B.hdr, B.data_offset,
                        crypto::verify_devkit_sig(&src[y.sec_info_offset() + 8]), B.sec);
    B.hdr.insert(B.hdr.end(), src.begin() + y.data_offset(), src.end());
    return B.hdr;
}

} // namespace xex
