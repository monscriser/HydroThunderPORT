// Standalone harness: round-trip + byte-identity test for the LZX encoder.
//   test_lzxenc <input-basefile> <expected-lzx-stream|-> [maxbytes]
#include "../src/compress/lzx.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

static std::vector<uint8_t> readf(const char* p) {
    FILE* f = std::fopen(p, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", p); std::exit(2); }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> v(n); if (std::fread(v.data(), 1, n, f) != (size_t)n) {}
    std::fclose(f); return v;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <basefile> <expected|-> [maxbytes]\n", argv[0]); return 2; }
    std::vector<uint8_t> in = readf(argv[1]);
    if (argc >= 4) { size_t m = std::strtoul(argv[3], nullptr, 0); if (m < in.size()) in.resize(m); }

    std::printf("compressing %zu bytes (window 0x8000, no E8)...\n", in.size());
    auto out = xex::compress::lzx_compress(in.data(), in.size(), 0x8000, 0);
    std::printf("=> %zu compressed bytes\n", out.size());
    std::printf("mine[0..23]:");
    for (int i = 0; i < 24 && i < (int)out.size(); i++) std::printf(" %02x", out[i]);
    std::printf("\n");
    if (const char* op = (argc >= 5 ? argv[4] : nullptr)) {
        FILE* g = std::fopen(op, "wb"); std::fwrite(out.data(), 1, out.size(), g); std::fclose(g);
    }

    // round-trip: decode and compare to input
    auto back = xex::compress::lzx_decompress(out.data(), out.size(), in.size(), 0x8000);
    bool rt = (back.size() == in.size() && std::memcmp(back.data(), in.data(), in.size()) == 0);
    std::printf("round-trip: %s\n", rt ? "OK (decodes back to input)" : "FAIL");
    if (!rt) {
        size_t i = 0; while (i < in.size() && i < back.size() && back[i] == in[i]) i++;
        std::printf("  first diff at %zu (in=%02x back=%02x)\n", i,
                    i < in.size() ? in[i] : 0, i < back.size() ? back[i] : 0);
    }

    if (std::strcmp(argv[2], "-") != 0) {
        auto exp = readf(argv[2]);
        bool id = (out.size() == exp.size() && std::memcmp(out.data(), exp.data(), out.size()) == 0);
        std::printf("byte-identical vs %s (%zu bytes): %s\n", argv[2], exp.size(),
                    id ? "YES ***" : "no");
        if (!id) {
            size_t i = 0, n = out.size() < exp.size() ? out.size() : exp.size();
            while (i < n && out[i] == exp[i]) i++;
            std::printf("  sizes mine=%zu exp=%zu; first diff @%zu (mine=%02x exp=%02x)\n",
                        out.size(), exp.size(), i, i < out.size() ? out[i] : 0, i < exp.size() ? exp[i] : 0);
        }
    }
    return 0;
}
