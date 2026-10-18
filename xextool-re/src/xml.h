// xml.h - machine-readable XML output.
#pragma once
#include "xex_file.h"
#include <string>

namespace xex {
// Print <XexInfo> XML. opts is a set of letters (a=all, b/d/m/p/r/t/x as in the
// original -x mode).
void print_xml(const XexFile& x, const std::string& opts);
}
