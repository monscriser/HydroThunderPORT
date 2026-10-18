// main.cpp - XexTool-RE command line entry point.
//
// A clean-room, modernized rebuild of xorloser's XexTool (2006-2011). This is
// the foundation: file loading, XEX2 parsing, and the `info` command. Crypto
// (AES/SHA-1/RSA), compression (LZX), patching and extraction land on top of
// this same parsed model.
#include "xex_file.h"
#include "info.h"
#include "basefile.h"
#include "extract.h"
#include "xml.h"
#include "modify.h"
#include "idc.h"
#include "special.h"
#include "convert.h"
#include "patch.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace {

const char* kVersion = "XexTool-RE 1.0.0";

void usage() {
    std::printf(
        "%s - a modern, open rebuild of XexTool\n"
        "\n"
        "Usage:  xextool-re <command> <file.xex> [args]\n"
        "\n"
        "Commands:\n"
        "  info      <file.xex>          summary info\n"
        "  list      <file.xex>          extended info (libs, resources, sections)\n"
        "  extract   <file.xex> [out]    dump the decrypted/decompressed basefile (-b)\n"
        "  resources <file.xex> [dir]    dump embedded resources (-d)\n"
        "  xml       <file.xex> [opts]   machine-readable XML, opts of a/b/d/m/p/r/t/x (-x)\n"
        "  idc       <file.xex> <out.idc>  dump an IDA script (-i)\n"
        "  compress   <file.xex> <out> [enc|unenc]  LZX-compress (-c c)\n"
        "  decompress <file.xex> <out>   convert to uncompressed basic (-c u)\n"
        "  binary     <file.xex> <out>   convert to flat binary basic (-c b)\n"
        "  encrypt|decrypt <file> <out>  toggle encryption (-e e / -e u)\n"
        "  machine   <file.xex> <d|r> <out>  force machine type (-m d / -m r)\n"
        "  remove-limits <file> <opts> <out>   strip limits (-r)\n"
        "      opts: a=all m=media r=region z=mediaid b=boundpath d=deviceid\n"
        "            l=min library version\n"
        "  bounding-path <file> <path> <out>   add a bounding path (-a)\n"
        "  patch <base.xex> <patch.xexp> <out>  apply a delta patch (-p/-u)\n"
        "  selftest                      run crypto known-answer tests\n",
        kVersion);
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize n = f.tellg();
    if (n < 0) return false;
    f.seekg(0);
    out.resize(size_t(n));
    return bool(f.read(reinterpret_cast<char*>(out.data()), n));
}

} // namespace

int run_selftest(); // selftest.cpp

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "selftest") return run_selftest();

    // patch <base.xex> <patch.xexp> <out.xex>  (the original's -p/-u)
    if (argc >= 2 && (std::string(argv[1]) == "patch" || std::string(argv[1]) == "-p")) {
        if (argc < 5) { std::fprintf(stderr, "usage: patch <base.xex> <patch.xexp> <out.xex>\n"); return 1; }
        std::vector<uint8_t> baseBytes, patchBytes;
        if (!read_file(argv[2], baseBytes))  { std::fprintf(stderr, "error: cannot read %s\n", argv[2]); return 1; }
        if (!read_file(argv[3], patchBytes)) { std::fprintf(stderr, "error: cannot read %s\n", argv[3]); return 1; }
        try {
            std::vector<uint8_t> outv = xex::apply_patch(baseBytes, patchBytes);
            std::ofstream of(argv[4], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outv.data()), outv.size());
            std::printf("applied patch -> %s (%zu bytes)\n", argv[4], outv.size());
        } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
        return 0;
    }

    if (argc < 3) { usage(); return argc < 2 ? 1 : 0; }

    std::string cmd = argv[1];
    std::string path = argv[2];

    std::vector<uint8_t> image;
    if (!read_file(path, image)) {
        std::fprintf(stderr, "error: cannot read file '%s'\n", path.c_str());
        return 1;
    }

    try {
        xex::XexFile x = xex::XexFile::parse(std::move(image));
        if (cmd == "info") {
            xex::print_info(x, /*extended=*/false);
        } else if (cmd == "list" || cmd == "-l") {
            xex::print_info(x, /*extended=*/true);
        } else if (cmd == "extract" || cmd == "dumpbase" || cmd == "-b") {
            std::string out = (argc >= 4) ? argv[3] : (path + ".basefile");
            xex::extract_basefile(x, out);
            std::printf("wrote basefile (%X bytes) -> %s\n", x.security().image_size, out.c_str());
        } else if (cmd == "resources" || cmd == "-d") {
            std::string dir = (argc >= 4) ? argv[3] : ".";
            int n = xex::dump_resources(x, dir);
            std::printf("dumped %d resource%s to %s\n", n, n == 1 ? "" : "s", dir.c_str());
        } else if (cmd == "xml" || cmd == "-x") {
            std::string opts = (argc >= 4) ? argv[3] : "a";
            xex::print_xml(x, opts);
        } else if (cmd == "remove-limits" || cmd == "-r") {
            if (argc < 5) {
                std::fprintf(stderr, "usage: remove-limits <file.xex> <opts> <out.xex>\n"
                                     "  opts: r=region m=media z=mediaid b=boundpath d=deviceid a=all\n");
                return 1;
            }
            std::string opts = argv[3];
            std::string out  = argv[4];
            std::vector<uint8_t> mod = xex::remove_limitations(x, opts);
            std::ofstream of(out, std::ios::binary);
            of.write(reinterpret_cast<const char*>(mod.data()), mod.size());
            std::printf("removed limits [%s] -> %s\n", opts.c_str(), out.c_str());
        } else if (cmd == "decompress" || cmd == "binary") {
            if (argc < 4) { std::fprintf(stderr, "usage: %s <file.xex> <out.xex>\n", cmd.c_str()); return 1; }
            std::vector<uint8_t> outbuf = xex::decompress_to_basic(x, cmd == "binary");
            std::ofstream of(argv[3], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outbuf.data()), outbuf.size());
            std::printf("decompressed -> %s (%zu bytes)\n", argv[3], outbuf.size());
        } else if (cmd == "compress") {
            if (argc < 4) { std::fprintf(stderr, "usage: compress <file.xex> <out.xex> [enc|unenc]\n"); return 1; }
            bool enc = x.is_encrypted();
            if (argc >= 5) enc = (std::string(argv[4]) == "enc");
            std::vector<uint8_t> outbuf = xex::compress_to_normal(x, enc);
            std::ofstream of(argv[3], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outbuf.data()), outbuf.size());
            std::printf("compressed -> %s (%zu bytes)\n", argv[3], outbuf.size());
        } else if (cmd == "encrypt" || cmd == "decrypt") {
            if (argc < 4) { std::fprintf(stderr, "usage: %s <file.xex> <out.xex>\n", cmd.c_str()); return 1; }
            std::vector<uint8_t> outbuf = xex::set_encryption(x, cmd == "encrypt");
            std::ofstream of(argv[3], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outbuf.data()), outbuf.size());
            std::printf("%sed -> %s\n", cmd.c_str(), argv[3]);
        } else if (cmd == "special") {
            // -s: with no mask, list what applies; otherwise apply the mask.
            std::vector<xex::SpecialPatch> avail = xex::special_patches_for(x);
            if (argc < 4) {
                std::printf("Special patches available for this file are:\n");
                if (avail.empty())
                    std::printf("  no special patch found for this xex\n");
                for (const auto& sp : avail)
                    std::printf("  %u = %s\n", sp.flag, sp.description.c_str());
                return 0;
            }
            if (argc < 5) {
                std::fprintf(stderr, "usage: special <file.xex> <mask> <out.xex>\n");
                return 1;
            }
            uint32_t mask = uint32_t(std::strtoul(argv[3], nullptr, 0));
            uint32_t applied = 0;
            std::vector<uint8_t> outbuf = xex::apply_special_patches(x, mask, &applied);
            for (const auto& sp : avail) {
                if (!(mask & sp.flag)) continue;
                std::printf((applied & sp.flag) ? "  patched %s\n"
                                                : "  error patching %s\n",
                            sp.description.c_str());
            }
            if (!applied) { std::fprintf(stderr, "no patches applied\n"); return 1; }
            std::ofstream of(argv[4], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outbuf.data()), outbuf.size());
            std::printf("special -> %s (%zu bytes)\n", argv[4], outbuf.size());
        } else if (cmd == "idc") {
            if (argc < 4) { std::fprintf(stderr, "usage: idc <file.xex> <out.idc>\n"); return 1; }
            std::string s = xex::make_idc(x);
            std::ofstream of(argv[3], std::ios::binary);
            of.write(s.data(), s.size());
            std::printf("idc -> %s (%zu bytes)\n", argv[3], s.size());
        } else if (cmd == "bounding-path") {
            if (argc < 5) { std::fprintf(stderr, "usage: bounding-path <file.xex> <path> <out.xex>\n"); return 1; }
            std::vector<uint8_t> outbuf = xex::add_bounding_path(x, argv[3]);
            std::ofstream of(argv[4], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outbuf.data()), outbuf.size());
            std::printf("bounding path added -> %s\n", argv[4]);
        } else if (cmd == "machine") {
            if (argc < 5) { std::fprintf(stderr, "usage: machine <file.xex> <d|r> <out.xex>\n"); return 1; }
            bool to_devkit = (std::string(argv[3]) != "r");
            std::vector<uint8_t> outbuf = xex::convert_machine(x, to_devkit);
            std::ofstream of(argv[4], std::ios::binary);
            of.write(reinterpret_cast<const char*>(outbuf.data()), outbuf.size());
            std::printf("machine %s -> %s (%zu bytes)\n", to_devkit ? "devkit" : "retail",
                        argv[4], outbuf.size());
        } else {
            std::fprintf(stderr, "error: unknown command '%s'\n\n", cmd.c_str());
            usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
