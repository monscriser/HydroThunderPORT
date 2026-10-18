// special.cpp - xex-specific "special" patches (the original's -s).
//
// These are hardcoded, per-system-file code patches. xextool picks the patch set
// from the execution-id's TitleID: system files carry TitleID == 0 and are then
// matched by PE name, so xam.xex ("xamd.dll") and xbdm.xex ("xbdm.dll") each get
// their own set and every real title gets none.
//
// The patches themselves are pattern searches over the decompressed image, not
// fixed offsets -- which is how they survive across builds of the same file. The
// replacement code is not stored in xextool's binary as data; it is built on the
// stack at runtime, which is why searching the binary for it finds nothing.
#include "special.h"
#include "basefile.h"
#include "convert.h"
#include <cstring>
#include <algorithm>

namespace xex {
namespace {

bool name_has(const XexFile& x, const char* needle) {
    auto pe = x.original_pe_name();
    if (pe && pe->find(needle) != std::string::npos) return true;
    return false;
}

bool is_system_file(const XexFile& x) {
    auto ex = x.execution_id();
    return ex && ex->title_id == 0;      // system files have no title id
}

// Find `pat` in `img`; returns npos-like (size_t)-1 when absent.
size_t find_bytes(const std::vector<uint8_t>& img, const uint8_t* pat, size_t n,
                  size_t from = 0) {
    if (n == 0 || img.size() < n) return size_t(-1);
    auto it = std::search(img.begin() + from, img.end(), pat, pat + n);
    return it == img.end() ? size_t(-1) : size_t(it - img.begin());
}

// ---- xam -------------------------------------------------------------------

// -s 1: force all arcade games to be licensed/unlocked/full.
// Find the call site `li r11,0x57 ; mr r3,r11`, walk BACKWARDS to the enclosing
// function's `mflr r12` prologue, and stub the function to write 1 through r3 and
// return 0. Verified byte-identical on xam 17489 (pattern @0x1D1274 -> prologue
// @0x1D1238).
bool xam_force_arcade(std::vector<uint8_t>& img) {
    static const uint8_t kSearch[8]  = {0x39,0x60,0x00,0x57, 0x7d,0x63,0x5b,0x78};
    static const uint8_t kPrologue[4] = {0x7d,0x88,0x02,0xa6};      // mflr r12
    static const uint8_t kStub[20] = {
        0x3d,0x60,0x00,0x00,   // lis  r11, 0
        0x61,0x6b,0x00,0x01,   // ori  r11, r11, 1
        0x91,0x63,0x00,0x00,   // stw  r11, 0(r3)
        0x38,0x60,0x00,0x00,   // li   r3, 0
        0x4e,0x80,0x00,0x20,   // blr
    };
    size_t hit = find_bytes(img, kSearch, sizeof kSearch);
    if (hit == size_t(-1)) return false;
    size_t p = hit;
    while (p > 0 && std::memcmp(&img[p], kPrologue, 4) != 0) --p;
    if (p == 0 || p + sizeof kStub > img.size()) return false;
    std::memcpy(&img[p], kStub, sizeof kStub);
    return true;
}

// -s 4: load load.xex instead of dash.xex -- a plain string swap in .text.
bool xam_load_xex(std::vector<uint8_t>& img) {
    static const uint8_t kFrom[9] = {'d','a','s','h','.','x','e','x',0};
    static const uint8_t kTo[9]   = {'l','o','a','d','.','x','e','x',0};
    size_t hit = find_bytes(img, kFrom, sizeof kFrom);
    if (hit == size_t(-1)) return false;
    std::memcpy(&img[hit], kTo, sizeof kTo);
    return true;
}

// -s 2: allow unauthenticated kinect hw usage. A masked search for the check,
// then flip the guarding conditional branch (`40 80 ....` = bne/bge) into an
// unconditional one (`48 00 ....`). NB xam 17489 does not contain this pattern
// -- xextool itself reports "error patching" there.
bool xam_kinect(std::vector<uint8_t>& img) {
    static const uint32_t kMask[5]  = {0xffffffff,0xfffff001,0xfc000001,0xffffffff,0xfffff000};
    static const uint32_t kPatA[5]  = {0x38600008,0x4bfffcb0,0x4bffeac9,0x2c030000,0x40800044};
    static const uint32_t kPatB[5]  = {0x38600008,0x4bfffcb0,0x4bffeac9,0x7c7d1b79,0x40800044};
    auto be32 = [&](size_t o) {
        return (uint32_t(img[o])<<24)|(uint32_t(img[o+1])<<16)|(uint32_t(img[o+2])<<8)|img[o+3];
    };
    auto masked_find = [&](const uint32_t* pat) -> size_t {
        if (img.size() < 20) return size_t(-1);
        for (size_t o = 0; o + 20 <= img.size(); o += 4) {
            bool ok = true;
            for (int i = 0; i < 5 && ok; ++i)
                if ((be32(o + i * 4) & kMask[i]) != (pat[i] & kMask[i])) ok = false;
            if (ok) return o;
        }
        return size_t(-1);
    };
    size_t hit = masked_find(kPatA);
    if (hit == size_t(-1)) hit = masked_find(kPatB);
    if (hit == size_t(-1)) return false;
    // Within the next 0x20 bytes, turn the `40 80` conditional into `48 00`.
    for (size_t o = 0; o < 0x20 && hit + o + 1 < img.size(); o += 4) {
        if (img[hit + o] == 0x40 && img[hit + o + 1] == 0x80) {
            img[hit + o] = 0x48; img[hit + o + 1] = 0x00;
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<SpecialPatch> special_patches_for(const XexFile& x) {
    if (!is_system_file(x)) return {};
    if (name_has(x, "xam"))
        return {{1, "force all arcade games to be licensed/unlocked/full"},
                {2, "allow unauthenticated kinect hw usage"},
                {4, "loads load.xex instead of dash.xex"}};
    // xbdm's two patches (extra drives / slim devkit flagging) are listed by the
    // original but their search patterns are not reverse-engineered yet, so they
    // are deliberately not offered rather than applied wrongly.
    return {};
}

std::vector<uint8_t> apply_special_patches(const XexFile& x, uint32_t flags,
                                           uint32_t* applied) {
    if (applied) *applied = 0;
    std::vector<SpecialPatch> avail = special_patches_for(x);
    if (avail.empty() || flags == 0) return x.raw();

    std::vector<uint8_t> image = reconstruct_basefile(x);
    uint32_t done = 0;
    if (name_has(x, "xam")) {
        if ((flags & 1) && xam_force_arcade(image)) done |= 1;
        if ((flags & 2) && xam_kinect(image))       done |= 2;
        if ((flags & 4) && xam_load_xex(image))     done |= 4;
    }
    if (applied) *applied = done;
    if (!done) return x.raw();

    // The image lives inside the XEX, so the file has to be rebuilt around it:
    // re-serialize in the source's format and re-sign. That is exactly what the
    // original does -- which is why `-s` on an MS-compressed file like xam grows
    // it (3190784 -> 3368960): xextool recompresses with its own encoder.
    return reserialize_with_image(x, image);
}

} // namespace xex
