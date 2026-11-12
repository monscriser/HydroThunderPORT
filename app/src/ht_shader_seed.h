// Shader pre-compilation for every track (same idea as the UFC3MONS launcher).
//
// The package ships only the LIST of guest shaders and pipeline descriptions the game uses on all
// 11 tracks (shaders/<platform>/, built by tools/shaders/shader_seed.cpp from a full play session;
// no compiled GPU code from any machine). Before the game starts, the list is merged into the
// player's own shader storage (only what is missing is appended; the player's records stay first),
// and the game then creates every pipeline with the player's driver for the player's GPU, with the
// "Preparando shaders" progress screen (ht_shader_progress.h). The SDK skips what the GPU cannot do.
// ht_shader_precompile=false (launcher) skips both the merge and the up-front creation.

#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ht::shaderseed {

namespace fs = std::filesystem;
inline constexpr const char* kTitle = "5841096A";
#if defined(_WIN32)
inline constexpr const char* kPlatform = "windows";
inline constexpr size_t kPipelineRecord = 68;  // PipelineStoredDescription, MS bitfield layout
#else
inline constexpr const char* kPlatform = "linux";
inline constexpr size_t kPipelineRecord = 66;  // same struct, Itanium packed bitfields
#endif

struct Result { size_t shaders = 0, pipelines = 0; bool seed_found = false, format_mismatch = false; };

namespace detail {
inline std::string Bytes(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  return std::string((std::istreambuf_iterator<char>(f)), {});
}
inline uint64_t U64(const std::string& s, size_t o) { uint64_t v; std::memcpy(&v, s.data() + o, 8); return v; }
inline uint32_t U32(const std::string& s, size_t o) { uint32_t v; std::memcpy(&v, s.data() + o, 4); return v; }
// .xsh: 8-byte header, then {u64 ucode hash, u32 dword count (31 bits) | type, ucode dwords}.
inline std::vector<std::pair<uint64_t, std::string>> Shaders(const std::string& d) {
  std::vector<std::pair<uint64_t, std::string>> out;
  size_t o = 8;
  while (o + 12 <= d.size()) {
    const size_t n = size_t(U32(d, o + 8) & 0x7FFFFFFFu) * 4;
    if (o + 12 + n > d.size()) break;
    out.emplace_back(U64(d, o), d.substr(o, 12 + n));
    o += 12 + n;
  }
  return out;
}
inline void Atomic(const fs::path& target, const std::string& data) {
  fs::create_directories(target.parent_path());
  const fs::path tmp = target.string() + ".seed-tmp";
  { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f.write(data.data(), std::streamsize(data.size())); }
  std::error_code ec;
  fs::rename(tmp, target, ec);
  if (ec) { fs::remove(target, ec); fs::rename(tmp, target, ec); }
}
// Appends the seed records the player's file lacks. Returns added count, SIZE_MAX on format mismatch.
inline size_t Merge(const fs::path& mine, const std::string& s, size_t header, size_t record) {
  if (s.size() < header) return 0;
  std::string m = Bytes(mine);
  if (m.size() < header) m = s.substr(0, header);
  else if (m.compare(0, header, s, 0, header) != 0) return SIZE_MAX;
  std::unordered_set<uint64_t> have;
  std::string add;
  size_t added = 0;
  if (record) {
    if ((s.size() - header) % record) return SIZE_MAX;
    m.resize(header + (m.size() - header) / record * record);  // the game also drops a torn last record
    for (size_t o = header; o < m.size(); o += record) have.insert(U64(m, o));
    for (size_t o = header; o < s.size(); o += record)
      if (have.insert(U64(s, o)).second) { add.append(s, o, record); ++added; }
  } else {
    size_t valid = 8;
    for (auto& r : Shaders(m)) { have.insert(r.first); valid += r.second.size(); }
    m.resize(valid);
    for (auto& r : Shaders(s))
      if (have.insert(r.first).second) { add += r.second; ++added; }
  }
  if (added) Atomic(mine, m + add);
  return added;
}
}  // namespace detail

// seed_root: folder next to the executable that holds shaders/<platform>/.
// cache_root: the game's shader cache root (storage lives in cache_root/shaders/shareable).
inline Result Merge(const fs::path& seed_root, const fs::path& cache_root) {
  Result r;
  const fs::path seed = seed_root / "shaders" / kPlatform, mine = cache_root / "shaders" / "shareable";
  const std::string xsh = std::string(kTitle) + ".xsh", xpso = std::string(kTitle) + ".fbo.vk.xpso";
  const std::string sp = detail::Bytes(seed / xpso), sx = detail::Bytes(seed / xsh);
  if (sp.size() < 12 || sx.size() < 8) return r;
  r.seed_found = true;
  // Check both headers first so a mismatch never leaves one file merged and the other not.
  const std::string mp = detail::Bytes(mine / xpso), mx = detail::Bytes(mine / xsh);
  if ((mp.size() >= 12 && mp.compare(0, 12, sp, 0, 12) != 0) || (mx.size() >= 8 && mx.compare(0, 8, sx, 0, 8) != 0)) {
    r.format_mismatch = true;
    return r;
  }
  const size_t shaders = detail::Merge(mine / xsh, sx, 8, 0);
  const size_t pipelines = detail::Merge(mine / xpso, sp, 12, kPipelineRecord);
  if (shaders == SIZE_MAX || pipelines == SIZE_MAX) { r.format_mismatch = true; return r; }
  r.shaders = shaders;
  r.pipelines = pipelines;
  return r;
}

}  // namespace ht::shaderseed
