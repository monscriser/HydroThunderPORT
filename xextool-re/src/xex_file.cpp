// xex_file.cpp - XEX2 parser implementation.
#include "xex_file.h"
#include <stdexcept>

namespace xex {

const OptionalHeader* XexFile::find_header(uint32_t key) const {
    for (const auto& h : opt_headers_)
        if (h.key == key) return &h;
    return nullptr;
}

bool XexFile::is_retail() const {
    // Machine format (retail vs devkit) is established by which RSA key signs
    // the header hash. Proper detection lands with the RSA layer; until then we
    // report retail, which is correct for the overwhelming majority of files.
    // TODO(crypto): verify security_.rsa_signature against retail/devkit keys.
    return true;
}

void XexFile::parse_main_header(const Reader& r) {
    if (r.size() < kMainHeaderSize || r.u32(0) != kXex2Magic)
        throw std::runtime_error("not a XEX2 file (bad magic)");
    module_flags_    = r.u32(0x04);
    data_offset_     = r.u32(0x08);
    sec_info_offset_ = r.u32(0x10);
    uint32_t count   = r.u32(0x14);
    for (uint32_t i = 0; i < count; ++i) {
        size_t off = 0x18 + size_t(i) * 8;
        opt_headers_.push_back({ r.u32(off), r.u32(off + 4) });
    }
}

void XexFile::parse_security_info(const Reader& r) {
    const size_t s = sec_info_offset_;
    security_.header_size = r.u32(s + 0);
    security_.image_size  = r.u32(s + 4);
    security_.rsa_signature = r.bytes(s + 8, 256);

    const size_t img = s + 8 + 256;     // image info block
    security_.image_flags  = r.u32(img + 4);
    security_.load_address = r.u32(img + 8);
    // img+12: section_digest[20]
    security_.import_table_count = r.u32(img + 32);
    // img+36: import_digest[20]
    security_.media_id = r.bytes(img + 56, 16);
    security_.aes_key  = r.bytes(img + 72, 16);
    security_.export_table = r.u32(img + 88);
    // img+92: header_digest[20]
    security_.region        = r.u32(img + 112);
    security_.allowed_media = r.u32(img + 116);
    security_.page_count    = r.u32(img + 120);
}

void XexFile::parse_sections(const Reader& r) {
    const size_t img = sec_info_offset_ + 8 + 256;
    const size_t descs = img + 124;          // page descriptors begin here
    uint32_t addr = security_.load_address;
    uint64_t total_pages = 0;

    struct Raw { uint8_t type; uint32_t pages; };
    std::vector<Raw> raws;
    for (uint32_t i = 0; i < security_.page_count; ++i) {
        uint32_t sai = r.u32(descs + size_t(i) * 24);
        raws.push_back({ uint8_t(sai & 0xF), sai >> 4 });
        total_pages += sai >> 4;
    }
    // Page size is robustly recovered as image_size / total_pages
    // (4 KiB for system modules, 64 KiB for most titles).
    if (total_pages) page_size_ = uint32_t(security_.image_size / total_pages);
    if (page_size_ == 0) page_size_ = 0x1000;

    for (const auto& raw : raws) {
        PageDescriptor d;
        d.type  = raw.type;
        d.pages = raw.pages;
        d.begin = addr;
        addr += raw.pages * page_size_;
        d.end = addr;
        sections_.push_back(d);
    }
}

// Resolve an optional header to a file offset for its variable-length payload.
// (Used only for the 0xFF "size-prefixed blob" headers.)
static size_t blob_offset(const OptionalHeader& h) { return h.value; }

void XexFile::parse_optional_headers(const Reader& r) {
    // Inline / fixed-size value headers --------------------------------
    if (auto* h = find_header(kHdrImageBaseAddr))  image_base_  = h->value;
    if (auto* h = find_header(kHdrEntryPoint))     entry_point_ = h->value;
    if (!entry_point_ && image_base_) entry_point_ = image_base_; // default

    if (auto* h = find_header(kHdrChecksumTimestamp)) {
        checksum_  = r.u32(h->value + 0);
        timestamp_ = r.u32(h->value + 4);
    }
    if (auto* h = find_header(kHdrLanKey))
        lan_key_ = r.bytes(h->value, 16);

    if (auto* h = find_header(kHdrExecutionId)) {
        size_t o = h->value;
        ExecutionId e;
        e.media_id     = r.u32(o + 0);
        e.version      = Version::unpack(r.u32(o + 4));
        e.base_version = Version::unpack(r.u32(o + 8));
        e.title_id     = r.u32(o + 12);
        e.platform     = r.u8(o + 16);
        e.exec_type    = r.u8(o + 17);
        e.disc_number  = r.u8(o + 18);
        e.disc_count   = r.u8(o + 19);
        e.savegame_id  = r.u32(o + 20);
        exec_id_ = e;
    }

    // Size-prefixed blob headers ---------------------------------------
    if (auto* h = find_header(kHdrFileFormatInfo)) {
        size_t o = blob_offset(*h);
        encryption_  = r.u16(o + 4);
        compression_ = r.u16(o + 6);
        if (compression_ == kCompNormal) {   // LZX: window + first block descriptor
            comp_window_size_      = r.u32(o + 8);
            comp_first_block_size_ = r.u32(o + 12);
        }
    }
    if (auto* h = find_header(kHdrOriginalPeName)) {
        size_t o = blob_offset(*h);
        pe_name_ = r.cstr(o + 4, r.u32(o));
    }
    if (auto* h = find_header(kHdrBoundingPath)) {
        size_t o = blob_offset(*h);
        bounding_path_ = r.cstr(o + 4, r.u32(o));
    }
    if (auto* h = find_header(kHdrResourceInfo)) {
        size_t o = blob_offset(*h);
        uint32_t size = r.u32(o);
        uint32_t n = (size >= 4) ? (size - 4) / 16 : 0;
        for (uint32_t i = 0; i < n; ++i) {
            size_t e = o + 4 + size_t(i) * 16;
            Resource res;
            res.name    = r.cstr(e, 8);
            res.address = r.u32(e + 8);
            res.size    = r.u32(e + 12);
            resources_.push_back(res);
        }
    }
    if (auto* h = find_header(kHdrStaticLibraries)) {
        size_t o = blob_offset(*h);
        uint32_t size = r.u32(o);
        uint32_t n = (size >= 4) ? (size - 4) / 16 : 0;
        for (uint32_t i = 0; i < n; ++i) {
            size_t e = o + 4 + size_t(i) * 16;
            StaticLibrary lib;
            lib.name = r.cstr(e, 8);
            lib.version.major = uint8_t(r.u16(e + 8));
            lib.version.minor = uint8_t(r.u16(e + 10));
            lib.version.build = r.u16(e + 12);
            lib.version.qfe   = uint8_t(r.u16(e + 14) & 0xFF); // 8-bit qfe slot
            static_libs_.push_back(lib);
        }
    }
    if (auto* h = find_header(kHdrImportLibraries)) {
        size_t o = blob_offset(*h);
        uint32_t string_table_size = r.u32(o + 4);
        uint32_t count = r.u32(o + 8);
        size_t strtab = o + 12;
        // Split the string table into a list of NUL-terminated names.
        std::vector<std::string> names;
        for (size_t p = 0; p < string_table_size; ) {
            std::string nm = r.cstr(strtab + p, string_table_size - p);
            names.push_back(nm);
            p += nm.size() + 1;
            while (p < string_table_size && r.u8(strtab + p) == 0) ++p; // padding
        }
        size_t desc = strtab + string_table_size;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t dsize = r.u32(desc);
            ImportLibrary lib;
            // descriptor: size@0, digest[20]@4, id@24, version@28,
            // version_min@32, name_index@36, import_count@38
            lib.version     = Version::unpack(r.u32(desc + 28));
            lib.version_min = Version::unpack(r.u32(desc + 32));
            uint16_t name_index = r.u16(desc + 36);
            if (name_index < names.size()) lib.name = names[name_index];
            import_libs_.push_back(lib);
            desc += dsize;
        }
    }
}

XexFile XexFile::parse(std::vector<uint8_t> file_image) {
    XexFile x;
    x.image_ = std::move(file_image);
    Reader r(x.image_);
    x.parse_main_header(r);
    x.parse_security_info(r);
    x.parse_sections(r);
    x.parse_optional_headers(r);
    return x;
}

} // namespace xex
