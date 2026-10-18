// special.h - xex-specific "special" patches (the original's -s).
#pragma once
#include "xex_file.h"
#include <string>
#include <vector>
#include <cstdint>

namespace xex {

// One hardcoded patch offered for a particular system file.
struct SpecialPatch {
    uint32_t flag;             // bitflag selecting it (1, 2, 4, ...)
    std::string description;   // exactly as the original prints it
};

// Which patches apply to this XEX. xextool dispatches on the execution-id's
// TitleID: system files have TitleID == 0 and are then matched by PE name
// ("xam" / "xbdm"). Anything with a real title id (every game and homebrew)
// gets nothing. Empty => "no special patch found for this xex".
std::vector<SpecialPatch> special_patches_for(const XexFile& x);

// Apply the selected patches (a bitmask of SpecialPatch::flag). The image is
// patched and the file re-serialized in its source format and re-signed, which
// is what the original does. `applied` receives the flags that actually matched
// their search pattern; a requested flag missing from it is the original's
// "error patching ..." case. Returns the source unchanged if nothing applied.
std::vector<uint8_t> apply_special_patches(const XexFile& x, uint32_t flags,
                                           uint32_t* applied = nullptr);

} // namespace xex
