// idc.cpp - generate an IDA IDC script describing the basefile (-i).
//
// The script is mostly fixed boilerplate (idc_boilerplate.h, lifted verbatim
// from a reference run). Only four things are derived from the XEX: the PE
// section map, the resource map, the import thunks, and the entry point.
#include "idc.h"
#include "idc_boilerplate.h"
#include "basefile.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>

namespace xex {
namespace {

uint32_t le32(const std::vector<uint8_t>& d, size_t o) {   // PE headers are LE
    return uint32_t(d[o]) | (uint32_t(d[o+1]) << 8) |
           (uint32_t(d[o+2]) << 16) | (uint32_t(d[o+3]) << 24);
}
uint16_t le16(const std::vector<uint8_t>& d, size_t o) {
    return uint16_t(uint32_t(d[o]) | (uint32_t(d[o+1]) << 8));
}
uint32_t be32(const std::vector<uint8_t>& d, size_t o) {
    return (uint32_t(d[o]) << 24) | (uint32_t(d[o+1]) << 16) |
           (uint32_t(d[o+2]) << 8) | d[o+3];
}
uint16_t be16(const std::vector<uint8_t>& d, size_t o) {
    return uint16_t((uint32_t(d[o]) << 8) | d[o+1]);
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap; va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

struct Section { std::string name; uint32_t start, end; bool code; int perms; };

// Sections straight out of the PE headers at the start of the image. IDA segment
// permissions: EXEC=1, WRITE=2, READ=4, matching the PE characteristics bits.
std::vector<Section> pe_sections(const std::vector<uint8_t>& img, uint32_t base) {
    std::vector<Section> out;
    if (img.size() < 0x40 || img[0] != 'M' || img[1] != 'Z') return out;
    uint32_t pe = le32(img, 0x3C);
    if (size_t(pe) + 24 > img.size()) return out;
    uint16_t nsec  = le16(img, pe + 6);
    uint16_t optsz = le16(img, pe + 20);
    size_t st = size_t(pe) + 24 + optsz;
    for (uint16_t i = 0; i < nsec; ++i) {
        size_t e = st + size_t(i) * 40;
        if (e + 40 > img.size()) break;
        char nm[9] = {0};
        std::memcpy(nm, &img[e], 8);
        uint32_t vsize = le32(img, e + 8), vaddr = le32(img, e + 12);
        uint32_t ch    = le32(img, e + 36);
        Section s;
        s.name  = nm;
        s.start = base + vaddr;
        s.end   = s.start + vsize;
        s.code  = (ch & 0x20000000u) != 0;                       // MEM_EXECUTE
        s.perms = ((ch & 0x20000000u) ? 1 : 0) |                 // exec
                  ((ch & 0x80000000u) ? 2 : 0) |                 // write
                  ((ch & 0x40000000u) ? 4 : 0);                  // read
        out.push_back(std::move(s));
    }
    return out;
}

std::string mangle(const std::string& s) {                      // xam.xex -> xam_xex
    std::string r = s;
    for (char& c : r) if (!isalnum((unsigned char)c)) c = '_';
    return r;
}

} // namespace

std::string make_idc(const XexFile& x) {
    std::vector<uint8_t> img = reconstruct_basefile(x);
    const uint32_t base = x.security().load_address;

    // Resources first: xextool only emits the PE sections that live below the
    // first resource (the ones above it -- .idata/.XBLD/.reloc -- are covered by
    // the resource map instead).
    const std::vector<Resource>& res = x.resources();
    uint32_t first_res = 0xFFFFFFFFu;
    for (const Resource& r : res) first_res = std::min(first_res, r.address);

    std::string body;

    // ---- SetupSections ------------------------------------------------------
    body += "static SetupSections()\n{\n    auto addr;\n\n";
    int idx = 1;
    for (const Section& s : pe_sections(img, base)) {
        if (s.start >= first_res) continue;
        body += fmt("    SetupSection(0x%08X, 0x%08X, \"%s\", %d, \"%s\", %d);\n",
                    s.start, s.end, s.code ? "CODE" : "DATA", s.perms,
                    s.name.c_str(), idx++);
    }
    body += "\n    // remove unused \"leftovers\" of the original binary segment\n"
            "    while( (addr = SegByBase(0)) != BADADDR )\n"
            "        DelSeg(addr, SEGMOD_KILL|SEGMOD_SILENT);\n}\n\n";

    // ---- SetupResources (segment index continues from the section map) ------
    body += "static SetupResources()\n{\n";
    for (const Resource& r : res)
        body += fmt("    SetupSection(0x%08X, 0x%08X, \"DATA\", 4, \"%s\", %d);\n",
                    r.address, r.address + r.size, r.name.c_str(), idx++);
    body += "}\n";

    body += kIdcMid1;

    // ---- setupImports_<lib>_<chunk> ----------------------------------------
    // Each library's record array holds VAs. A function import is a PAIR:
    // rec[i] is the IAT slot (image word 0x0000<ord>) and rec[i+1] the stub
    // (0x0100<ord>, same ordinal). A lone slot is a data import. Records are
    // chunked 256 at a time -- note that is 256 RECORDS, not 256 emitted lines.
    const OptionalHeader* imp = x.find_header(kHdrImportLibraries);
    std::vector<std::string> chunk_names;
    if (imp) {
        const std::vector<uint8_t>& raw = x.raw();
        size_t it = imp->value;
        uint32_t str_size = be32(raw, it + 4);
        uint32_t nlibs    = be32(raw, it + 8);
        size_t p = it + 12 + str_size;
        for (uint32_t L = 0; L < nlibs && p + 0x28 <= raw.size(); ++L) {
            uint32_t bsz    = be32(raw, p);
            uint16_t ncount = be16(raw, p + 0x26);
            const std::string& lib = L < x.import_libraries().size()
                                   ? x.import_libraries()[L].name : std::string();
            std::vector<uint32_t> recs(ncount);
            for (uint16_t i = 0; i < ncount; ++i) recs[i] = be32(raw, p + 0x28 + size_t(i) * 4);

            auto word_at = [&](uint32_t va) -> uint32_t {
                size_t o = size_t(va - base);
                return (o + 4 <= img.size()) ? be32(img, o) : 0u;
            };
            for (uint32_t c0 = 0; c0 < ncount; c0 += 256) {
                uint32_t c1 = std::min<uint32_t>(c0 + 256, ncount);
                std::string name = fmt("setupImports_%u_%s_%u", L,
                                       mangle(lib).c_str(), c0 / 256);
                chunk_names.push_back(name);
                body += "static " + name + "()\n{\n";
                for (uint32_t i = c0; i < c1;) {
                    uint32_t v = word_at(recs[i]);
                    uint32_t ord = v & 0xFFFF;
                    bool is_func = (i + 1 < c1) && ((word_at(recs[i + 1]) >> 24) == 1) &&
                                   ((word_at(recs[i + 1]) & 0xFFFF) == ord);
                    if (is_func) {
                        body += fmt("    SetupImportFunc(0x%08X, 0x%08X, 0x%03X, \"%s\");\n",
                                    recs[i], recs[i + 1], ord, lib.c_str());
                        i += 2;
                    } else {
                        body += fmt("    SetupImportData(0x%08X,             0x%03X, \"%s\");\n",
                                    recs[i], ord, lib.c_str());
                        i += 1;
                    }
                }
                body += "}\n\n";
            }
            p += bsz;
        }
    }
    body += "static SetupImports()\n{\n";
    for (const std::string& n : chunk_names) body += "    " + n + "();\n";
    body += "}\n";

    body += kIdcMid2;

    // ---- SetupExports (entry point only) ------------------------------------
    uint32_t entry = x.entry_point().value_or(0);
    body += "static SetupExports()\n{\n    auto name;\n    name = GetInputFile();\n\n\n"
            "    // set start entry point\n";
    // No trailing blank line here: kIdcTail opens with one.
    body += fmt("    SetupExportFunc(0x%08X, 0x%08X, \"start\");\n}\n", entry, entry);

    std::string out = std::string(kIdcHead) + body + kIdcTail;

    // The reference output is CRLF throughout.
    std::string crlf;
    crlf.reserve(out.size() + out.size() / 16);
    for (char c : out) { if (c == '\n') crlf += '\r'; crlf += c; }
    return crlf;
}

} // namespace xex
