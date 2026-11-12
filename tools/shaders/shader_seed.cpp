// Builds the "shader seed" shipped with the game: the LIST of guest shaders (.xsh) and pipeline
// descriptions (.fbo.vk.xpso) the game uses on every track. At start-up the game merges it into the
// player's own shader storage and compiles everything with the player's driver for the player's GPU
// (the SDK skips what the GPU cannot do). Nothing compiled for another machine is used or shipped
// (the "local/*.bin" native caches are never read).
//
// Pipeline records differ between the Linux build (Itanium packed bitfields: 66 bytes) and the
// Windows build (MS bitfields: 68 bytes). PipelineDescription = 4 x u64 + u32 render pass key +
// 45 bits of state (Linux: 6 bytes, continuous; Windows: two u32 units, bits 32..44 move to the
// second unit) + 4 x u32 render targets. Every record's hash (XXH3 of the description) is checked
// on input and recomputed on output, exactly like the SDK does (pipeline_cache.cpp).
//
// usage: shader_seed <out_dir> <in_dir:linux|windows>...   (each in_dir holds 5841096A.xsh/.xpso)
// writes <out_dir>/linux/ and <out_dir>/windows/ with the union of all inputs.
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static const char* kTitle = "5841096A";

static std::string Read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), {});
}
static void Write(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  f.write(s.data(), std::streamsize(s.size()));
}
static uint64_t U64(const char* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
static uint32_t U32(const char* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

// Canonical description: 36 bytes of hashes + key, 45 state bits as u64, 16 bytes of render targets.
struct Desc { char head[36]; uint64_t state; char rts[16]; };

static bool Decode(const char* rec, bool windows, Desc& d) {
  std::memset(&d, 0, sizeof(d));  // padding bytes take part in dedupe/compare
  const size_t dsize = windows ? 60 : 58;
  const char* desc = rec + 8;
  if (XXH3_64bits(desc, dsize) != U64(rec)) return false;
  std::memcpy(d.head, desc, 36);
  if (windows) {
    const uint32_t a = U32(desc + 36), b = U32(desc + 40);
    if (b >> 13) return false;  // only 13 bits live in the second unit
    d.state = uint64_t(a) | (uint64_t(b) << 32);
    std::memcpy(d.rts, desc + 44, 16);
  } else {
    uint64_t v = 0;
    std::memcpy(&v, desc + 36, 6);
    if (v >> 45) return false;  // 45 state bits
    d.state = v;
    std::memcpy(d.rts, desc + 42, 16);
  }
  return true;
}
static std::string Encode(const Desc& d, bool windows) {
  std::string desc(windows ? 60 : 58, '\0');
  std::memcpy(desc.data(), d.head, 36);
  if (windows) {
    const uint32_t a = uint32_t(d.state), b = uint32_t(d.state >> 32);
    std::memcpy(desc.data() + 36, &a, 4);
    std::memcpy(desc.data() + 40, &b, 4);
    std::memcpy(desc.data() + 44, d.rts, 16);
  } else {
    std::memcpy(desc.data() + 36, &d.state, 6);
    std::memcpy(desc.data() + 42, d.rts, 16);
  }
  const uint64_t h = XXH3_64bits(desc.data(), desc.size());
  return std::string(reinterpret_cast<const char*>(&h), 8) + desc;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: shader_seed <out> <dir:linux|windows>...\n"); return 2; }
  const fs::path out = argv[1];
  std::string xpso_header, xsh_header;
  std::vector<Desc> pipelines;
  std::set<std::string> seen_desc;  // canonical bytes, for dedupe across inputs
  std::vector<std::string> shaders;
  std::set<uint64_t> seen_shader;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    const auto colon = arg.rfind(':');
    const fs::path dir = arg.substr(0, colon);
    const bool windows = arg.substr(colon + 1) == "windows";
    const size_t rec = windows ? 68 : 66;
    const std::string p = Read(dir / (std::string(kTitle) + ".fbo.vk.xpso"));
    const std::string x = Read(dir / (std::string(kTitle) + ".xsh"));
    if (p.size() < 12 || x.size() < 8) { std::fprintf(stderr, "%s: missing storage\n", dir.c_str()); return 1; }
    if (xpso_header.empty()) xpso_header = p.substr(0, 12);
    if (xsh_header.empty()) xsh_header = x.substr(0, 8);
    if (p.compare(0, 12, xpso_header) || x.compare(0, 8, xsh_header)) {
      std::fprintf(stderr, "%s: different storage format/version\n", dir.c_str());
      return 1;
    }
    size_t ok = 0, bad = 0, dup = 0, torn = (p.size() - 12) % rec;
    for (size_t o = 12; o + rec <= p.size(); o += rec) {
      Desc d;
      if (!Decode(p.data() + o, windows, d)) { ++bad; continue; }
      const std::string key(reinterpret_cast<const char*>(&d), sizeof(d));
      if (!seen_desc.insert(key).second) { ++dup; continue; }
      pipelines.push_back(d);
      ++ok;
    }
    size_t sok = 0, sdup = 0;
    for (size_t o = 8; o + 12 <= x.size();) {
      const size_t n = size_t(U32(x.data() + o + 8) & 0x7FFFFFFFu) * 4;
      if (o + 12 + n > x.size()) break;
      if (seen_shader.insert(U64(x.data() + o)).second) { shaders.push_back(x.substr(o, 12 + n)); ++sok; } else ++sdup;
      o += 12 + n;
    }
    std::printf("%s (%s): pipelines ok=%zu bad_hash=%zu duplicate=%zu torn_bytes=%zu | shaders new=%zu duplicate=%zu\n",
                dir.c_str(), windows ? "windows" : "linux", ok, bad, dup, torn, sok, sdup);
    if (bad) return 1;
  }
  std::string xsh = xsh_header;
  for (const auto& s : shaders) xsh += s;
  for (const bool windows : {false, true}) {
    std::string xpso = xpso_header;
    for (const auto& d : pipelines) xpso += Encode(d, windows);
    // Round trip: every written record must decode back to the same description.
    const size_t rec = windows ? 68 : 66;
    for (size_t i = 0, o = 12; i < pipelines.size(); ++i, o += rec) {
      Desc back;
      if (!Decode(xpso.data() + o, windows, back) || std::memcmp(&back, &pipelines[i], sizeof(Desc))) {
        std::fprintf(stderr, "round trip failed\n");
        return 1;
      }
    }
    const fs::path dir = out / (windows ? "windows" : "linux");
    Write(dir / (std::string(kTitle) + ".fbo.vk.xpso"), xpso);
    Write(dir / (std::string(kTitle) + ".xsh"), xsh);
    std::printf("wrote %s: %zu pipelines, %zu shaders\n", dir.c_str(), pipelines.size(), shaders.size());
  }
  return 0;
}
