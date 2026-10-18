// extract.cpp - basefile and resource extraction.
#include "extract.h"
#include "basefile.h"
#include <fstream>
#include <stdexcept>

namespace xex {

static void write_file(const std::string& path, const uint8_t* data, size_t n) {
    std::ofstream of(path, std::ios::binary);
    if (!of) throw std::runtime_error("cannot write file: " + path);
    of.write(reinterpret_cast<const char*>(data), n);
}

void extract_basefile(const XexFile& x, const std::string& out_path) {
    std::vector<uint8_t> base = reconstruct_basefile(x);
    write_file(out_path, base.data(), base.size());
}

int dump_resources(const XexFile& x, const std::string& outdir) {
    if (x.resources().empty()) return 0;
    std::vector<uint8_t> base = reconstruct_basefile(x);
    const uint32_t load = x.security().load_address;

    int count = 0;
    for (const auto& r : x.resources()) {
        if (r.address < load) continue;                 // not in the image
        size_t off = r.address - load;
        size_t n = r.size;
        if (off > base.size()) continue;
        if (off + n > base.size()) n = base.size() - off;  // clamp tail
        write_file(outdir + "/" + r.name, base.data() + off, n);
        ++count;
    }
    return count;
}

} // namespace xex
