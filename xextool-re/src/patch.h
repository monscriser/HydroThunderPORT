// patch.h - apply a .xexp delta patch to a base XEX (the original's -p/-u).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xex {

// Apply a delta patch (patchBytes) to a base XEX (baseBytes). Produces a new,
// standalone XEX image (decrypted + uncompressed "basic" format, i.e. the
// original's -u behavior). Throws std::runtime_error with a reason on failure.
std::vector<uint8_t> apply_patch(const std::vector<uint8_t>& baseBytes,
                                 const std::vector<uint8_t>& patchBytes);

} // namespace xex
