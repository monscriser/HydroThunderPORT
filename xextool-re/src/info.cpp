// info.cpp - human-readable XEX information dump (the `info` command).
#include "info.h"
#include "basefile.h"
#include <cstdio>
#include <ctime>

namespace xex {

static void hexline(const std::vector<uint8_t>& b) {
    std::printf("  ");
    for (uint8_t c : b) std::printf("%02X ", c);
    std::printf("\n");
}

static std::string format_filetime(uint32_t t) {
    static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    // The original tool renders the timestamp in local time; match that.
    time_t tt = (time_t)t;
    std::tm g{};
#if defined(_WIN32)
    localtime_s(&g, &tt);
#else
    localtime_r(&tt, &g);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %s %2d %02d:%02d:%02d %d",
                  wd[g.tm_wday], mo[g.tm_mon], g.tm_mday,
                  g.tm_hour, g.tm_min, g.tm_sec, g.tm_year + 1900);
    return buf;
}

static void print_regions(uint32_t region) {
    std::printf("\nRegions\n");
    if (region == kRegionAll) { std::printf("  All Regions\n"); return; }
    if (region == 0)          { std::printf("  No Game Region\n"); return; }
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
        if ((region & r.mask) == r.mask) std::printf("  %s\n", r.name);
}

static void print_media(uint32_t media) {
    std::printf("\nAllowed Media\n");
    if (media == 0xFFFFFFFF) { std::printf("  All Media Types\n"); return; }
    struct { uint32_t mask; const char* name; } M[] = {
        { 0x00000001, "Hard Disk" },
        { 0x00000002, "DVD-X2 (Xbox1 Original Disc)" },
        { 0x00000004, "DVD / CD" },
        { 0x00000008, "DVD5" },
        { 0x00000010, "DVD9" },
        { 0x00000020, "System Flash" },
        { 0x00000080, "Memory Unit" },
        { 0x00000100, "Usb Mass Storage" },
        { 0x00000200, "Network" },
        { 0x00000400, "Direct From Memory" },
        { 0x00001000, "Secure Virtual Optical Device (SVOD)" },
        { 0x01000000, "Insecure Package" },
        { 0x02000000, "Savegame Package (\"CONS\")" },
        { 0x04000000, "Locally Signed Package" },
        { 0x08000000, "Xbox Live Signed Package" },
        { 0x10000000, "Xbox Package" },
    };
    bool any = false;
    for (auto& m : M)
        if (media & m.mask) { std::printf("  %s\n", m.name); any = true; }
    if (!any) std::printf("  (none / unknown 0x%08X)\n", media);
}

void print_info(const XexFile& x, bool extended) {
    // ---- Xex Info -----------------------------------------------------
    std::printf("Xex Info\n");
    std::printf("  %s\n", is_retail_xex(x) ? "Retail" : "Devkit");
    std::printf("  %s\n", x.is_compressed() ? "Compressed" : "Not-Compressed");
    std::printf("  %s\n", x.is_encrypted() ? "Encrypted" : "Not-Encrypted");
    uint32_t mf = x.module_flags();
    if (mf & kModTitle)          std::printf("  Title Module\n");
    if (mf & kModExportsToTitle) std::printf("  Exports To Title\n");
    if (mf & kModSystemDebugger) std::printf("  System Debugger\n");
    if (mf & kModDll)            std::printf("  DLL Module\n");
    if (mf & kModModulePatch)    std::printf("  Module Patch\n");
    if (mf & kModPatchFull)      std::printf("  Full Patch\n");
    if (mf & kModPatchDelta)     std::printf("  Delta Patch\n");
    if (mf & kModUserMode)       std::printf("  User Mode\n");
    // "No Game Region" denotes a region-free module (no game-region lock).
    uint32_t rgn = x.security().region;
    if (rgn == 0 || rgn == kRegionAll) std::printf("  No Game Region\n");

    // ---- Basefile Info -----------------------------------------------
    std::printf("\nBasefile Info\n");
    if (auto n = x.original_pe_name())
        std::printf("  Original PE Name:   %s\n", n->c_str());
    if (auto a = x.image_base())  std::printf("  Load Address:       %08X\n", *a);
    if (auto e = x.entry_point()) std::printf("  Entry Point:        %08X\n", *e);
    std::printf("  Image Size:         %8X\n", x.security().image_size);
    std::printf("  Page Size:          %8X\n", x.page_size());
    if (auto c = x.checksum())  std::printf("  Checksum:           %08X\n", *c);
    std::printf("  Export Table:       %08X\n", x.security().export_table);
    if (auto t = x.timestamp())
        std::printf("  Filetime:           %08X - %s\n", *t, format_filetime(*t).c_str());

    print_regions(x.security().region);
    print_media(x.security().allowed_media);

    std::printf("\nMedia Id \n");        hexline(x.security().media_id);
    // For a non-encrypted xex there is no effective key; the original shows
    // zeros. (Once the AES layer lands this will show the decrypted key.)
    std::printf("\nEncryption Key \n");
    if (x.is_encrypted()) hexline(x.security().aes_key);
    else                  hexline(std::vector<uint8_t>(16, 0));
    if (auto k = x.lan_key()) { std::printf("\nLAN Key \n"); hexline(*k); }

    if (auto bp = x.bounding_path())
        std::printf("\nBounding Path\n  %s\n", bp->c_str());

    if (auto e = x.execution_id()) {
        std::printf("\nExecution Id\n");
        std::printf("  Media Id:           %08X\n", e->media_id);
        uint32_t tid = e->title_id;
        char a = char((tid >> 24) & 0xFF), b = char((tid >> 16) & 0xFF);
        if (a >= 0x20 && a < 0x7F && b >= 0x20 && b < 0x7F)
            std::printf("  Title Id:           %08X  (%c%c-%d)\n", tid, a, b, tid & 0xFFFF);
        else
            std::printf("  Title Id:           %08X\n", tid);
        std::printf("  Savegame Id:        %08X\n", e->savegame_id);
        std::printf("  Version:            %s\n", e->version.str().c_str());
        std::printf("  Base Version:       %s\n", e->base_version.str().c_str());
        std::printf("  Platform:           %X\n", e->platform);
        std::printf("  Executable Type:    %X\n", e->exec_type);
        std::printf("  Disc Number:        %d\n", e->disc_number);
        std::printf("  Number of Discs:    %d\n", e->disc_count);
    }

    if (!extended) return;

    // ---- Extended sections -------------------------------------------
    if (!x.static_libraries().empty()) {
        std::printf("\nStatic Libraries\n");
        int i = 0;
        for (auto& l : x.static_libraries())
            std::printf("  %3d) %-14s %s\n", i++, l.name.c_str(), l.version.str().c_str());
    }
    if (!x.import_libraries().empty()) {
        std::printf("\nImport Libraries\n");
        int i = 0;
        for (auto& l : x.import_libraries())
            std::printf("  %3d) %-14s %s  (min %s)\n", i++, l.name.c_str(),
                        l.version.str().c_str(), l.version_min.str().c_str());
    }
    if (!x.resources().empty()) {
        std::printf("\nResources\n");
        int i = 0;
        for (auto& r : x.resources())
            std::printf("  %3d) %08X - %08X : %s\n", i++, r.address,
                        r.address + r.size, r.name.c_str());
    }
    if (!x.sections().empty()) {
        std::printf("\nSections\n");
        int i = 0;
        for (auto& s : x.sections())
            std::printf("  %3d) %08X - %08X : %s\n", i++, s.begin, s.end,
                        section_type_name(s.type));
    }
}

} // namespace xex
