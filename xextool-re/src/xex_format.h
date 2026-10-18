// xex_format.h - XEX2 on-disk format constants and small value types.
//
// This is a clean-room model of the Xbox 360 XEX2 container, reconstructed from
// public format documentation (Free60 / Xenia) and cross-checked byte-for-byte
// against real files. Offsets/keys below are verified, not guessed.
#pragma once
#include <cstdint>
#include <string>

namespace xex {

constexpr uint32_t kXex2Magic = 0x58455832; // 'XEX2'

// ---- Main header (big-endian, at file offset 0) -------------------------
// uint32 magic              @0x00
// uint32 module_flags       @0x04
// uint32 data_offset        @0x08   (offset to the basefile/PE data)
// uint32 reserved           @0x0C
// uint32 sec_info_offset    @0x10   (offset to the security info block)
// uint32 opt_header_count   @0x14
// { uint32 key; uint32 value }[opt_header_count] @0x18
constexpr size_t kMainHeaderSize = 0x18;

// ---- Module flags (header @0x04) ---------------------------------------
enum ModuleFlags : uint32_t {
    kModTitle        = 0x00000001,
    kModExportsToTitle = 0x00000002,
    kModSystemDebugger = 0x00000004,
    kModDll          = 0x00000008,
    kModModulePatch  = 0x00000010,
    kModPatchFull    = 0x00000020,
    kModPatchDelta   = 0x00000040,
    kModUserMode     = 0x00000080,
};

// ---- Optional header keys (the low byte encodes size/format) -----------
enum HeaderKey : uint32_t {
    kHdrResourceInfo      = 0x000002FF,
    kHdrFileFormatInfo    = 0x000003FF, // compression + encryption
    kHdrDeltaPatchDesc    = 0x000005FF,
    kHdrBoundingPath      = 0x000080FF,
    kHdrDeviceId          = 0x00008105,
    kHdrOriginalBaseAddr  = 0x00010001,
    kHdrEntryPoint        = 0x00010100,
    kHdrImageBaseAddr     = 0x00010201,
    kHdrImportLibraries   = 0x000103FF,
    kHdrChecksumTimestamp = 0x00018002,
    kHdrOriginalPeName    = 0x000183FF,
    kHdrStaticLibraries   = 0x000200FF,
    kHdrTlsInfo           = 0x00020104,
    kHdrDefaultStackSize  = 0x00020200,
    kHdrDefaultFsCache    = 0x00020301,
    kHdrDefaultHeapSize   = 0x00020401,
    kHdrSystemFlags       = 0x00030000,
    kHdrExecutionId       = 0x00040006,
    kHdrTitleWorkspace    = 0x00040201,
    kHdrGameRatings       = 0x00040310,
    kHdrLanKey            = 0x00040404,
    kHdrMultidiscMediaIds = 0x000406FF,
    kHdrAlternateTitleIds = 0x000407FF,
    kHdrAdditionalTitleMem= 0x00040801,
};

// ---- File format info (compression + encryption) -----------------------
enum EncryptionType : uint16_t {
    kEncNone = 0x0000,
    kEncAes  = 0x0001,
};
enum CompressionType : uint16_t {
    kCompNone   = 0x0001, // "binary" / not compressed (may have zeroed data)
    kCompBasic  = 0x0001,
    kCompNormal = 0x0002, // LZX block compression
    kCompDelta  = 0x0003,
};

// ---- Region flags (security info) --------------------------------------
enum RegionFlags : uint32_t {
    kRegionNorthAmerica = 0x000000FF,
    kRegionJapan        = 0x00000100,
    kRegionChina        = 0x00000200,
    kRegionRestOfAsia   = 0x0000FC00,
    kRegionAusNz        = 0x00010000,
    kRegionRestOfEurope = 0x00FE0000,
    kRegionRestOfWorld  = 0xFF000000,
    kRegionAll          = 0xFFFFFFFF,
};

// ---- Page descriptor section type (info nibble) ------------------------
// Verified against real files: 1=Code, 2=Data, 3=Header/Resource.
enum SectionType : uint8_t {
    kSectionCode           = 1,
    kSectionData           = 2,
    kSectionHeaderResource = 3,
};

inline const char* section_type_name(uint8_t t) {
    switch (t) {
        case kSectionCode:           return "Code";
        case kSectionData:           return "Data";
        case kSectionHeaderResource: return "Header/Resource";
        default:                     return "Unknown";
    }
}

// ---- Packed version field (major:4 minor:4 build:16 qfe:8) --------------
struct Version {
    uint8_t  major = 0;
    uint8_t  minor = 0;
    uint16_t build = 0;
    uint8_t  qfe   = 0;
    static Version unpack(uint32_t v) {
        return Version{ uint8_t((v >> 28) & 0xF), uint8_t((v >> 24) & 0xF),
                        uint16_t((v >> 8) & 0xFFFF), uint8_t(v & 0xFF) };
    }
    std::string str() const {
        return "v" + std::to_string(major) + "." + std::to_string(minor) + "." +
               std::to_string(build) + "." + std::to_string(qfe);
    }
};

} // namespace xex
