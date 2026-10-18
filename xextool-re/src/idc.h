// idc.h - generate an IDA IDC script describing the basefile (the original's -i).
#pragma once
#include "xex_file.h"
#include <string>

namespace xex {

// Produce the IDC script text, byte-identical to xextool's -i (CRLF line
// endings included). Most of the script is fixed boilerplate; only the section
// map, resource map, import thunks and entry point are derived from the XEX.
std::string make_idc(const XexFile& x);

} // namespace xex
