// basefile.h - reconstruct the raw basefile (PE image) from a XEX.
#pragma once
#include "xex_file.h"
#include <vector>
#include <cstdint>

namespace xex {

// Produce the decrypted, decompressed basefile image (image_size bytes).
// Handles encryption (AES-128-CBC with the unwrapped title key) and the
// "normal" (LZX) and "basic/none" compression formats. Throws on failure.
std::vector<uint8_t> reconstruct_basefile(const XexFile& x);

// Determine whether a XEX is retail- or devkit-keyed. For encrypted files this
// is decided by verifying the retail key against the first-block integrity hash
// (compressed) or the PE magic (uncompressed); unencrypted files are reported
// as retail (their signature isn't verifiable here). Returns true for retail.
bool is_retail_xex(const XexFile& x);

// Return the key-encryption-key (retail / devkit-zeros / XEX1) that correctly
// unwraps this XEX's title key, found by trial-decrypting the first block.
// Returns the retail key for unencrypted files.
const uint8_t* detect_kek(const XexFile& x);

} // namespace xex
