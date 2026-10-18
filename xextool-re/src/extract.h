// extract.h - basefile and resource extraction.
#pragma once
#include "xex_file.h"
#include <string>

namespace xex {

// Write the reconstructed basefile (decrypted + decompressed) to a path.
void extract_basefile(const XexFile& x, const std::string& out_path);

// Dump every embedded resource into outdir, named by its resource id.
// Returns the number of resources written.
int dump_resources(const XexFile& x, const std::string& outdir);

} // namespace xex
