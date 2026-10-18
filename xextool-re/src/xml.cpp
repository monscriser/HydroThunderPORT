// xml.cpp - machine-readable XML output (the original's -x mode).
//
// Mirrors the original's <XexInfo> schema so the tool can be driven from other
// programs. Parse-only (no decryption), so it's fast.
#include "xml.h"
#include "basefile.h"
#include <cstdio>
#include <string>

namespace xex {

namespace {

std::string basefile_type(uint32_t mf) {
    if (mf & (kModModulePatch | kModPatchFull | kModPatchDelta)) return "Patch";
    if (mf & kModTitle) return "Exe";
    if (mf & kModDll)   return "Dll";
    return "Other";
}

void emit_media(uint32_t media) {
    std::printf("    <GameMedias>\n");
    struct { uint32_t mask; const char* name; } M[] = {
        { 0x00000001, "Hard Disk" },
        { 0x00000002, "DVD-X2 (Xbox1 Original Disc)" },
        { 0x00000004, "DVD / CD" },
        { 0x00000008, "DVD5" },
        { 0x00000010, "DVD9" },
        { 0x00000020, "System Flash" },
        { 0x00000080, "Memory Unit" },
        { 0x00000100, "Usb Mass Storage" },
        { 0x02000000, "Savegame Package (\"CONS\")" },
    };
    for (auto& m : M)
        if (media & m.mask) std::printf("        <Media format=\"string\">%s</Media>\n", m.name);
    std::printf("    </GameMedias>\n");
}

void emit_regions(uint32_t region) {
    std::printf("    <GameRegions>\n");
    if (region == kRegionAll) {
        std::printf("        <Region format=\"string\">All</Region>\n");
    } else {
        struct { uint32_t mask; const char* name; } R[] = {
            { kRegionNorthAmerica, "North America" },
            { kRegionJapan,        "Japan" },
            { kRegionChina,        "China" },
            { kRegionRestOfAsia,   "Rest of Asia" },
            { kRegionAusNz,        "Australia / New Zealand" },
            { kRegionRestOfEurope, "Rest of Europe" },
            { kRegionRestOfWorld,  "Rest of the World" },
        };
        for (auto& r : R)
            if ((region & r.mask) == r.mask)
                std::printf("        <Region format=\"string\">%s</Region>\n", r.name);
    }
    std::printf("    </GameRegions>\n");
}

void hex_element(const char* tag, const std::vector<uint8_t>& b) {
    std::printf("    <%s format=\"hex\">", tag);
    for (uint8_t c : b) std::printf("%02x", c);
    std::printf("</%s>\n", tag);
}

} // namespace

void print_xml(const XexFile& x, const std::string& opts) {
    bool all = opts.find('a') != std::string::npos;
    auto want = [&](char c) { return all || opts.find(c) != std::string::npos; };

    std::printf("<XexInfo>\n");
    if (want('b'))
        std::printf("    <BasefileType format=\"string\">%s</BasefileType>\n",
                    basefile_type(x.module_flags()).c_str());
    if (want('d'))
        hex_element("MediaId", x.security().media_id);
    if (want('m'))
        emit_media(x.security().allowed_media);
    if (want('p') && x.bounding_path())
        std::printf("    <BoundingPath>%s</BoundingPath>\n", x.bounding_path()->c_str());
    if (want('r'))
        emit_regions(x.security().region);
    if (want('t')) {
        uint32_t tid = x.execution_id() ? x.execution_id()->title_id : 0;
        std::printf("    <TitleId format=\"hex\">%08x</TitleId>\n", tid);
    }
    if (want('x'))
        std::printf("    <MachineFormat format=\"string\">%s</MachineFormat>\n",
                    is_retail_xex(x) ? "Retail" : "Devkit");
    std::printf("</XexInfo>\n");
}

} // namespace xex
