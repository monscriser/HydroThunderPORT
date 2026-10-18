// convert.h - basefile format conversions (compression / encryption).
#pragma once
#include "xex_file.h"
#include <vector>

namespace xex {

// Convert to the uncompressed "basic" format with no LZX framing. Encryption is
// preserved (the stored payload is re-encrypted if the source was encrypted).
//   binary=false (the original's -c u): zero regions are not stored. The image
//     is segmented into alternating (data, zeros) runs on a 32 KB granule and
//     only the data runs are written.
//   binary=true  (the original's -c b): one run covering the whole image, zeros
//     included -- much larger, but a flat 1:1 image.
std::vector<uint8_t> decompress_to_basic(const XexFile& x, bool binary = false,
                                         const std::vector<uint8_t>* image_in = nullptr);

// Compress the basefile into the XEX "normal" (LZX block) format (the
// original's -c c). Byte-identical to xextool for the compressed data region
// and file-format-info. `encrypt` selects the output encryption state.
std::vector<uint8_t> compress_to_normal(const XexFile& x, bool encrypt,
                                        const std::vector<uint8_t>* image_in = nullptr);

// Rebuild a XEX around a modified basefile image, preserving the source's
// compression/encryption format and re-signing. Used by the special patches
// (-s), which edit the decompressed image and then need the file put back
// together around it.
std::vector<uint8_t> reserialize_with_image(const XexFile& x,
                                            const std::vector<uint8_t>& image);

// Force the basefile encryption state (the original's -e u / -e e). Encrypts or
// decrypts the basefile data region in place with the title key (unwrapped from
// the security info), preserving compression. A no-op if already in the target
// state. Returns the modified file image.
std::vector<uint8_t> set_encryption(const XexFile& x, bool encrypt);

// ---- canonical header rebuild ---------------------------------------------
// xextool rebuilds the header of every XEX it writes to one canonical layout:
//
//   0x00  base header (data_offset @+8, security-info offset @+0x10, count @+0x14)
//   0x18  optional-header directory, KEY-SORTED, count*8 bytes
//         0x80 zero bytes  <- always exactly this gap
//   sec   security info, where sec = 0x18 + count*8 + 0x80
//         (0x120 for the usual 17 entries, but 0xF8 for xam's 12, 0xE8 for
//          xbdm's 10 -- never assume 0x120; image-info is at sec+0x108)
//   ...   blobs, in key order, packed at their EXACT size (no alignment)
//   pad   zeros
//         import-libraries blob, FLUSH against the end of the header
//   data_offset = round_up(other_blobs_end + import_size, 0x1000)
//
// Verified against every xextool-produced sample. It matters: xbdm.xex ships
// with a wasted page (data_offset 0x2000 where the law says 0x1000) and xextool
// reclaims it, moving the import blob 0x1728 -> 0x728 and shrinking the file by
// 0x1000. data_offset depends on the file-format-info size, which changes per
// operation, so the header must be laid out BEFORE data_offset is known.
struct HeaderEntry {
    uint32_t key = 0;
    uint32_t val = 0;                 // inline value, or the assigned blob offset
    std::vector<uint8_t> body;        // blob payload (empty for inline entries)
    bool is_blob = false;
};
struct HeaderBuild {
    std::vector<uint8_t> hdr;         // [0, data_offset), fully laid out
    uint32_t data_offset = 0;
    uint32_t sec = 0;
    bool ok = false;
};

// Snapshot a XEX's optional-header directory (blob payloads included) so the
// caller can edit/insert/remove entries before rebuilding.
std::vector<HeaderEntry> header_entries(const XexFile& x);

// Lay the entries out per the law above. The security-info block is copied from
// the source; edit it (and re-sign) in the returned header afterwards.
//
// `repack` selects how the non-import blobs are placed:
//   true  - repacked contiguously in key order from sec+sec_size. Use when the
//           directory or a blob's size changed such that the old offsets no
//           longer describe a valid layout (-c u, -c b, -a).
//   false - each blob keeps the offset it had in the source. -c c and -e do NOT
//           repack: xextool rewrites the file-format-info in place and leaves
//           every following blob put, which is why CoD's cc1 carries a 0xC gap
//           after its FFI shrank 0x30 -> 0x24.
// data_offset and the import blob's position are recomputed either way.
HeaderBuild build_header(const XexFile& x, std::vector<HeaderEntry> ents,
                         bool repack = true);

// Force the output machine type (the original's -m d / -m r). Header-only
// transform: the basefile data is untouched. Normalizes the layout, rewraps the
// title key under the target KEK (devkit = zeros, retail = the retail master) so
// the unchanged ciphertext stays valid, and re-signs. Byte-identical to xextool.
//   to_devkit=true  (-m d): signs with the devkit key. A verbatim no-op when the
//                           input is already devkit.
//   to_devkit=false (-m r): ZEROES the signature -- there is no retail private
//                           key, so a retail-converted XEX is never re-signed.
std::vector<uint8_t> convert_machine(const XexFile& x, bool to_devkit);

inline std::vector<uint8_t> convert_machine_devkit(const XexFile& x) {
    return convert_machine(x, true);
}

} // namespace xex
