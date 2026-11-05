#pragma once
// Moving the whole user_data folder (saves, DLC, achievements) to a place the player picks.
// Nothing is ever deleted: the data is COPIED to the new place, and the old folder stays
// as it was (a backup the player may remove). If the new place already has a user_data
// with something in it, the player decides: use it as it is, or bring the current data
// (the folder that was there is renamed to user_data-anterior-<time>, not removed).
#include "ht_library_paths.h"
#include "ht_save_backup.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
namespace ht::userdata {
namespace fs = std::filesystem;
// The folder a choice points to: the picked folder itself when it is called user_data,
// otherwise a user_data folder inside it.
inline fs::path TargetFor(const fs::path& picked) {
  return picked.filename() == "user_data" ? picked : picked / "user_data";
}
inline bool HasData(const fs::path& folder) {
  std::error_code ec;
  if (!fs::is_directory(folder, ec)) return false;
  for (const auto& item : fs::recursive_directory_iterator(folder, ec))
    if (item.is_regular_file(ec)) return true;
  return false;
}
inline bool Same(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  if (fs::exists(a, ec) && fs::exists(b, ec)) return fs::equivalent(a, b, ec);
  return fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}
// Throws with a message for the player when the target cannot hold the data.
inline void Check(const fs::path& from, const fs::path& to) {
  if (saves::Within(to, from) || saves::Within(from, to))
    throw std::runtime_error("Escolha uma pasta que nao esteja dentro da pasta de dados atual (nem a contenha).");
#if defined(_WIN32)
  // Saves sit ~70 characters below user_data; Windows paths beyond ~250 fail without long-path support.
  if (to.wstring().size() > 160)
    throw std::runtime_error("O caminho escolhido e muito longo. Escolha uma pasta mais perto da raiz do disco (ex.: D:\\Jogos).");
#endif
  fs::create_directories(to);
  const auto probe = to / ".hydro-thunder-teste-de-escrita";
  { std::ofstream out(probe, std::ios::binary); out << "ok"; out.close();
    if (!out) throw std::runtime_error("Nao foi possivel gravar nessa pasta. Escolha outra.");
  }
  std::error_code ec;
  fs::remove(probe, ec);  // the probe file was created just above by this check
}
inline size_t CopyAll(const fs::path& from, const fs::path& to) {
  size_t count = 0;
  for (const auto& item : fs::recursive_directory_iterator(from)) {
    if (item.is_symlink()) continue;
    const auto dest = to / fs::relative(item.path(), from);
    if (item.is_directory()) { fs::create_directories(dest); continue; }
    if (!item.is_regular_file()) continue;
    fs::create_directories(dest.parent_path());
    fs::copy_file(item.path(), dest, fs::copy_options::overwrite_existing);
    std::error_code ec; fs::last_write_time(dest, fs::last_write_time(item.path(), ec), ec);  // keep the save's date
    ++count;
  }
  return count;
}
enum class Mode { kCopyCurrent, kUseExisting };
struct Result { fs::path from, to, kept_aside; size_t files = 0; bool used_existing = false; };
// to_default: back to <library>/user_data. Otherwise to = TargetFor(picked).
inline Result Change(const fs::path& library, const fs::path& to, bool to_default, Mode mode) {
  Result r;
  r.from = UserDataRoot(library);
  r.to = to;
  if (Same(r.from, r.to)) {
    RememberUserDataRoot(to_default ? fs::path{} : r.to);
    return r;
  }
  Check(r.from, r.to);
  if (mode == Mode::kUseExisting && HasData(r.to)) {
    r.used_existing = true;
  } else {
    if (HasData(r.to)) {
      const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
      r.kept_aside = r.to.parent_path() / ("user_data-anterior-" + stamp);
      fs::rename(r.to, r.kept_aside);
      fs::create_directories(r.to);
    }
    if (fs::is_directory(r.from)) r.files = CopyAll(r.from, r.to);
  }
  RememberUserDataRoot(to_default ? fs::path{} : r.to);
  return r;
}
}  // namespace ht::userdata
