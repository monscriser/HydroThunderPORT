// modify.h - in-place XEX header modifications (no re-compression).
//
// These operations patch the original file bytes directly, preserving the
// (compressed/encrypted) basefile exactly. The result is a minimal-diff,
// still-valid XEX with the requested limitations removed -- cleaner than the
// original tool, which fully re-serializes and re-compresses.
#pragma once
#include "xex_file.h"
#include <string>
#include <vector>

namespace xex {

// Remove limitations per the original's -r options (subset):
//   r = region, m = media, z = zero media id, b = bounding path,
//   d = device id, a = all of the above.
// Returns the modified file image.
std::vector<uint8_t> remove_limitations(const XexFile& x, const std::string& opts);

// Add a bounding path (the original's -a). Inserts optional header 0x000080FF --
// blob = [u32 size][path][NUL], size rounded up to 4 -- in key-sorted order,
// which grows the directory by one entry and therefore shifts the security info
// and every header blob. Re-signs. Byte-identical to xextool.
std::vector<uint8_t> add_bounding_path(const XexFile& x, const std::string& path);

} // namespace xex
