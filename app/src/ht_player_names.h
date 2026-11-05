#pragma once
// Player names (1.1 BETA). The game shows each local player's gamertag; the SDK profiles are
// "Player 1".."Player 4". The launcher stores the names in ht_player_name_1..4, and they are
// given to the profiles before the game reads them. Saves are filed by XUID, and the
// per-profile settings folder uses the original "Player N", so renaming never moves data.
#include <array>
#include <string>
#include <string_view>

namespace ht::players {

constexpr int kCount = 4;
// Xbox gamertags are at most 15 characters; the XAM calls copy into 16-byte buffers.
constexpr size_t kMaxLength = 15;
inline const char* Cvar(int i) {
  static const char* names[kCount] = {"ht_player_name_1", "ht_player_name_2", "ht_player_name_3",
                                      "ht_player_name_4"};
  return names[i];
}
inline std::string DefaultName(int i) { return "Player " + std::to_string(i + 1); }
// The game's fonts only cover printable ASCII. Quotes and backslashes are left out so the
// value is always a plain TOML string.
inline bool Allowed(unsigned c) { return c >= 0x20 && c <= 0x7E && c != '"' && c != '\\'; }
inline std::string Sanitize(std::string_view raw) {
  std::string name;
  for (char c : raw)
    if (Allowed(static_cast<unsigned char>(c))) name += c;
  const auto first = name.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  name = name.substr(first, name.find_last_not_of(' ') - first + 1);
  if (name.size() > kMaxLength) {
    name.resize(kMaxLength);
    name.erase(name.find_last_not_of(' ') + 1);
  }
  return name;
}
// The name to use for player i: the setting, or "Player N" when empty/unusable.
inline std::string Effective(int i, std::string_view setting) {
  auto name = Sanitize(setting);
  return name.empty() ? DefaultName(i) : name;
}

}  // namespace ht::players
