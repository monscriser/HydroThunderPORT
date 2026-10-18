// xex_file.h - parsed model of a XEX2 file.
#pragma once
#include "byteio.h"
#include "xex_format.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace xex {

struct OptionalHeader {
    uint32_t key   = 0;
    uint32_t value = 0; // either an inline value or a file offset (key low byte)
};

struct SecurityInfo {
    uint32_t header_size   = 0;
    uint32_t image_size    = 0;
    std::vector<uint8_t> rsa_signature;   // 256 bytes
    uint32_t image_flags   = 0;
    uint32_t load_address  = 0;
    uint32_t import_table_count = 0;
    std::vector<uint8_t> media_id;        // 16 bytes
    std::vector<uint8_t> aes_key;         // 16 bytes (encrypted title key)
    uint32_t export_table  = 0;
    uint32_t region        = 0;
    uint32_t allowed_media = 0;
    uint32_t page_count    = 0;
};

struct PageDescriptor {
    uint8_t  type   = 0;   // SectionType
    uint32_t pages  = 0;
    uint32_t begin  = 0;   // virtual address
    uint32_t end    = 0;
};

struct StaticLibrary {
    std::string name;
    Version version;
};

struct ImportLibrary {
    std::string name;
    Version version;
    Version version_min;
};

struct Resource {
    std::string name;
    uint32_t address = 0;
    uint32_t size    = 0;
};

struct ExecutionId {
    uint32_t media_id    = 0;
    Version  version;
    Version  base_version;
    uint32_t title_id    = 0;
    uint8_t  platform    = 0;
    uint8_t  exec_type   = 0;
    uint8_t  disc_number = 0;
    uint8_t  disc_count  = 0;
    uint32_t savegame_id = 0;
};

class XexFile {
public:
    // Parse from an in-memory file image. Throws std::runtime_error on bad data.
    static XexFile parse(std::vector<uint8_t> file_image);

    // --- main header ---
    uint32_t module_flags()    const { return module_flags_; }
    uint32_t data_offset()     const { return data_offset_; }
    uint32_t sec_info_offset() const { return sec_info_offset_; }

    const SecurityInfo& security() const { return security_; }
    const std::vector<OptionalHeader>& optional_headers() const { return opt_headers_; }
    const std::vector<PageDescriptor>& sections() const { return sections_; }
    uint32_t page_size() const { return page_size_; }

    // --- optional-header derived fields (present-or-not) ---
    std::optional<uint32_t> image_base()  const { return image_base_; }
    std::optional<uint32_t> entry_point() const { return entry_point_; }
    std::optional<std::string> original_pe_name() const { return pe_name_; }
    std::optional<std::string> bounding_path()    const { return bounding_path_; }
    std::optional<uint32_t> checksum()  const { return checksum_; }
    std::optional<uint32_t> timestamp() const { return timestamp_; }
    std::optional<std::vector<uint8_t>> lan_key() const { return lan_key_; }
    std::optional<ExecutionId> execution_id() const { return exec_id_; }
    std::optional<uint16_t> compression_type() const { return compression_; }
    std::optional<uint16_t> encryption_type()  const { return encryption_; }
    uint32_t comp_window_size()      const { return comp_window_size_; }
    uint32_t comp_first_block_size() const { return comp_first_block_size_; }

    // Raw file image and where the basefile data begins within it.
    const std::vector<uint8_t>& raw() const { return image_; }

    const std::vector<StaticLibrary>& static_libraries() const { return static_libs_; }
    const std::vector<ImportLibrary>& import_libraries() const { return import_libs_; }
    const std::vector<Resource>&      resources()        const { return resources_; }

    // --- convenience predicates ---
    bool is_retail()     const;     // machine format
    bool is_encrypted()  const { return encryption_.value_or(kEncNone) != kEncNone; }
    bool is_compressed() const { return compression_.value_or(kCompNone) == kCompNormal; }

    const OptionalHeader* find_header(uint32_t key) const;

private:
    void parse_main_header(const Reader& r);
    void parse_security_info(const Reader& r);
    void parse_sections(const Reader& r);
    void parse_optional_headers(const Reader& r);

    std::vector<uint8_t> image_;

    uint32_t module_flags_   = 0;
    uint32_t data_offset_    = 0;
    uint32_t sec_info_offset_= 0;
    std::vector<OptionalHeader> opt_headers_;

    SecurityInfo security_;
    std::vector<PageDescriptor> sections_;
    uint32_t page_size_ = 0x1000;

    std::optional<uint32_t> image_base_;
    std::optional<uint32_t> entry_point_;
    std::optional<std::string> pe_name_;
    std::optional<std::string> bounding_path_;
    std::optional<uint32_t> checksum_;
    std::optional<uint32_t> timestamp_;
    std::optional<std::vector<uint8_t>> lan_key_;
    std::optional<ExecutionId> exec_id_;
    std::optional<uint16_t> compression_;
    std::optional<uint16_t> encryption_;
    uint32_t comp_window_size_ = 0;
    uint32_t comp_first_block_size_ = 0;

    std::vector<StaticLibrary> static_libs_;
    std::vector<ImportLibrary> import_libs_;
    std::vector<Resource> resources_;
};

} // namespace xex
