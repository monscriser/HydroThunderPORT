#pragma once
// Save editor (1.1 BETA): reads and changes the credits in ht2save.dat.
//
// The save is a big-endian typed name/value tree ('AKJB', u32 1, root node):
//   node := u32 type, then: 0 nothing | 1 s32 | 2 f32 | 3 u8 | 4 u32 len + chars
//           | 5 u32 count + nodes | 6 u32 count + (u32 keylen + key, node)...
// The root object holds Crc32, Data and Version. On load the game checks
// Crc32 == CRC-32 of a walk over Data's *values* (type, value, counts and keys, but not
// key lengths). Format notes credited to htrecomp's tools/ht_save_edit.py (visibou).
// Only the credit value and the Crc32 value change, in place; the file keeps its size.
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ht_library_paths.h"
#include "ht_save_backup.h"

namespace ht::saveedit {
namespace fs = std::filesystem;

inline uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc) {
  static const auto table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

struct Parsed {
  std::vector<uint8_t> bytes;
  size_t crc_offset = 0;      // value bytes of root/Crc32 (s32)
  uint32_t stored_crc = 0;
  uint32_t computed_crc = 0;  // over root/Data, as the game computes it
  size_t data_offset = 0;     // node start of root/Data
  int32_t version = 0;
  struct Credit { std::string key; size_t offset = 0; int32_t value = 0; };
  std::vector<Credit> credits;  // Data/Unlock/*Credits (s32)
  std::optional<Credit> Find(std::string_view key) const {
    for (const auto& c : credits) if (c.key == key) return c;
    return std::nullopt;
  }
};

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}
  uint32_t U32(size_t& o) const {
    Need(o, 4);
    const uint32_t v = (uint32_t(b_[o]) << 24) | (uint32_t(b_[o + 1]) << 16) | (uint32_t(b_[o + 2]) << 8) | b_[o + 3];
    o += 4;
    return v;
  }
  void Need(size_t o, size_t n) const {
    if (o > b_.size() || b_.size() - o < n) throw std::runtime_error("save truncated");
  }
  // Walks one node starting at o (its type). Adds it to crc as the game does. 'path'
  // is the key path, used to note Data/Unlock/<x>Credits.
  void Node(size_t& o, uint32_t& crc, const std::string& path, Parsed& out, int depth) const {
    if (depth > 64) throw std::runtime_error("save nested too deep");
    const size_t type_at = o;
    const uint32_t type = U32(o);
    crc = Crc32(&b_[type_at], 4, crc);
    switch (type) {
      case 0: break;
      case 1: case 2: {
        Need(o, 4);
        if (type == 1 && path.rfind("Data/Unlock/", 0) == 0 && path.size() > 7 &&
            path.compare(path.size() - 7, 7, "Credits") == 0) {
          size_t t = o;
          out.credits.push_back({path.substr(12), o, int32_t(U32(t))});
        }
        crc = Crc32(&b_[o], 4, crc);
        o += 4;
        break;
      }
      case 3: Need(o, 1); crc = Crc32(&b_[o], 1, crc); o += 1; break;
      case 4: {
        const uint32_t len = U32(o);
        Need(o, len);
        size_t chars = 0;
        while (chars < len && b_[o + chars]) ++chars;  // the game hashes up to a NUL
        crc = Crc32(&b_[o], chars, crc);
        o += len;
        break;
      }
      case 5: {
        const size_t count_at = o;
        const uint32_t count = U32(o);
        crc = Crc32(&b_[count_at], 4, crc);
        for (uint32_t i = 0; i < count; ++i) Node(o, crc, path + "/[]", out, depth + 1);
        break;
      }
      case 6: {
        const size_t count_at = o;
        const uint32_t count = U32(o);
        crc = Crc32(&b_[count_at], 4, crc);
        for (uint32_t i = 0; i < count; ++i) {
          const uint32_t len = U32(o);
          Need(o, len);
          size_t chars = 0;
          while (chars < len && b_[o + chars]) ++chars;
          crc = Crc32(&b_[o], chars, crc);
          const std::string key(reinterpret_cast<const char*>(&b_[o]), chars);
          o += len;
          Node(o, crc, path.empty() ? key : path + "/" + key, out, depth + 1);
        }
        break;
      }
      default: throw std::runtime_error("unknown save value type");
    }
  }

 private:
  const std::vector<uint8_t>& b_;
};

inline Parsed Parse(std::vector<uint8_t> bytes) {
  Parsed p;
  p.bytes = std::move(bytes);
  Reader r(p.bytes);
  if (p.bytes.size() < 12 || std::memcmp(p.bytes.data(), "AKJB", 4) != 0)
    throw std::runtime_error("not a Hydro Thunder save (AKJB)");
  size_t o = 4;
  if (r.U32(o) != 1) throw std::runtime_error("unknown save format version");
  if (r.U32(o) != 6) throw std::runtime_error("save root is not an object");
  const uint32_t members = r.U32(o);
  bool have_crc = false, have_data = false;
  for (uint32_t i = 0; i < members; ++i) {
    const uint32_t len = r.U32(o);
    r.Need(o, len);
    const std::string key(reinterpret_cast<const char*>(&p.bytes[o]), len);
    o += len;
    const size_t node_at = o;
    if (key == "Crc32" || key == "Version") {
      size_t t = o;
      if (r.U32(t) != 1) throw std::runtime_error("save " + key + " is not an integer");
      const uint32_t v = r.U32(t);
      if (key == "Crc32") { p.crc_offset = node_at + 4; p.stored_crc = v; have_crc = true; }
      else p.version = int32_t(v);
      o = t;
    } else {
      uint32_t crc = 0;
      Parsed scratch;
      r.Node(o, crc, key, key == "Data" ? p : scratch, 0);
      if (key == "Data") { p.computed_crc = crc; p.data_offset = node_at; have_data = true; }
    }
  }
  if (!have_crc || !have_data) throw std::runtime_error("save without Crc32/Data");
  return p;
}

inline std::vector<uint8_t> ReadFile(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("cannot open the save");
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.size() > (16u << 20)) throw std::runtime_error("save too large");
  return bytes;
}

inline void PutU32(std::vector<uint8_t>& b, size_t o, uint32_t v) {
  b[o] = uint8_t(v >> 24); b[o + 1] = uint8_t(v >> 16); b[o + 2] = uint8_t(v >> 8); b[o + 3] = uint8_t(v);
}

// The four local players' profiles (see ht_player_names.h / SDK patch_player_profiles.py).
inline const char* ProfileFolder(int player) {
  static const char* ids[4] = {"B13EBABEBABEBABE", "B13EBABEBABEBABF", "B13EBABEBABEBAC0", "B13EBABEBABEBAC1"};
  return ids[player];
}
inline fs::path SaveFile(const fs::path& user_data, int player) {
  return user_data / ProfileFolder(player) / "5841096A" / "00000001" / "ht2save.dat" / "ht2save.dat";
}

struct Info {
  bool exists = false;
  bool valid = false;      // parsed and the checksum matches what the game expects
  int32_t total = 0, local = 0, online = 0;
  std::string error;
};
inline Info Load(const fs::path& user_data, int player) {
  Info info;
  const auto file = SaveFile(user_data, player);
  std::error_code ec;
  if (!fs::is_regular_file(file, ec)) return info;
  info.exists = true;
  try {
    const auto p = Parse(ReadFile(file));
    if (p.stored_crc != p.computed_crc) { info.error = "checksum"; return info; }
    if (p.version != 6) { info.error = "version"; return info; }
    const auto total = p.Find("TotalCredits");
    if (!total) { info.error = "credits"; return info; }
    info.total = total->value;
    if (auto c = p.Find("LocalMultiplayerCredits")) info.local = c->value;
    if (auto c = p.Find("OnlineMultiplayerCredits")) info.online = c->value;
    info.valid = true;
  } catch (const std::exception& e) {
    info.error = e.what();
  }
  return info;
}

constexpr int32_t kMaxCredits = 9'999'999;

// Sets Data/Unlock/TotalCredits and re-signs the save. The saves are exported to
// <library>/save_backups first; the new file replaces the old one atomically.
// Returns the backup folder.
inline fs::path SetCredits(const fs::path& library, int player, int32_t credits) {
  if (credits < 0 || credits > kMaxCredits) throw std::runtime_error("credits out of range");
  const auto file = SaveFile(UserDataRoot(library), player);
  auto p = Parse(ReadFile(file));
  if (p.stored_crc != p.computed_crc || p.version != 6)
    throw std::runtime_error("checksum");
  const auto total = p.Find("TotalCredits");
  if (!total) throw std::runtime_error("credits");
  PutU32(p.bytes, total->offset, uint32_t(credits));
  // Re-walk to get the new checksum, then check the result reads back as intended.
  auto edited = Parse(p.bytes);
  PutU32(edited.bytes, edited.crc_offset, edited.computed_crc);
  const auto check = Parse(edited.bytes);
  if (check.stored_crc != check.computed_crc || !check.Find("TotalCredits") ||
      check.Find("TotalCredits")->value != credits || check.bytes.size() != p.bytes.size())
    throw std::runtime_error("edit check failed");
  const auto backup = saves::Export(library, library / "save_backups");
  const auto temp = file.parent_path() / "ht2save.dat.ht-edit";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(check.bytes.data()), std::streamsize(check.bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("cannot write the save");
  }
  fs::rename(temp, file);
  return backup;
}

}  // namespace ht::saveedit
