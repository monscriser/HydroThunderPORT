// info.h - XEX information dump.
#pragma once
#include "xex_file.h"

namespace xex {
// Print a human-readable summary of a parsed XEX. `extended` adds the static
// libraries, imports, resources and section map (the original tool's `-l`).
void print_info(const XexFile& x, bool extended);
}
