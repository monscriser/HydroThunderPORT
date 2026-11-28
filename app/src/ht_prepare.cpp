#include "ht_prepare.h"

#include "ht_content_install.h"
#include "ht_library_paths.h"
#include "ht_save_backup.h"
#include "ht_setup.h"

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace ht::prepare {
namespace {
namespace fs = std::filesystem;

constexpr const char* kMarkerVersion = "HT-PREPARED-1";

// A reason for the player, in English, printed as the last line.
struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
void Require(bool condition, const char* message) {
  if (!condition) throw Failure(message);
}

void Progress(double value, const std::string& text) {
  std::printf("progress %.2f %s\n", value, text.c_str());
  std::fflush(stdout);
}

// PrepareContent reports its steps in Portuguese; the launcher shows these instead.
void ImportProgress(const std::string& step) {
  if (step.rfind("1/3", 0) == 0) Progress(0.05, "Unpacking your game");
  else if (step.rfind("3/3", 0) == 0) Progress(0.60, "Reading the title update");
  else if (step.rfind("Preparando", 0) == 0) Progress(0.70, "Updating the game to version 0.0.2.1");
  else if (step.rfind("Arquivos preparados", 0) == 0) Progress(0.88, "Game files ready");
  else Progress(0.50, step);
}

// Held for the whole run. The OS drops it when the process ends, however it ends.
class Lock {
 public:
  explicit Lock(const fs::path& path) {
#if defined(_WIN32)
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    const bool locked = handle_ != INVALID_HANDLE_VALUE;
#else
    fd_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    const bool locked = fd_ >= 0 && flock(fd_, LOCK_EX | LOCK_NB) == 0;
#endif
    if (!locked) {
      Release();
      throw Failure("Another setup of Hydro Thunder Hurricane is running. Wait for it to finish, then try again.");
    }
  }
  ~Lock() { Release(); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;

 private:
  void Release() {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
#else
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
#endif
  }
#if defined(_WIN32)
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int fd_ = -1;
#endif
};

bool Given(const fs::path& path) {
  std::error_code ec;
  return !path.empty() && fs::exists(path, ec);
}

// Which file a step used: its path, size and time, so a rerun with the same file is skipped.
std::string Identity(const fs::path& path) {
  std::error_code ec;
  auto absolute = fs::weakly_canonical(path, ec);
  if (ec) absolute = path;
  const uint64_t size = fs::is_regular_file(path, ec) ? fs::file_size(path, ec) : 0;
  const long long time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             fs::last_write_time(path, ec).time_since_epoch()).count();
  return std::to_string(size) + " " + std::to_string(time) + " " + rex::path_to_utf8(absolute);
}

using Marker = std::map<std::string, std::string>;
Marker ReadMarker(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string line;
  Marker marker;
  if (!std::getline(in, line) || line != kMarkerVersion) return marker;
  while (std::getline(in, line))
    if (const auto space = line.find(' '); space != std::string::npos)
      marker[line.substr(0, space)] = line.substr(space + 1);
  return marker;
}
void WriteMarker(const fs::path& file, const Marker& marker) {
  auto temp = file;
  temp += ".new";
  {
    std::ofstream out(temp, std::ios::binary);
    out << kMarkerVersion << "\n";
    for (const auto& [key, value] : marker) out << key << " " << value << "\n";
    out.close();
    Require(bool(out), "Couldn't write to the game's folder. Check the drive has free space.");
  }
  fs::rename(temp, file);
}

// Folders are replaced whole: the new one is put beside the live one, the live one is moved
// aside and removed last, so Recover can always put back a whole folder on the next run.
fs::path Beside(const fs::path& live, const char* suffix) {
  auto path = live;
  path += suffix;
  return path;
}
void Recover(const fs::path& live) {
  fs::remove_all(Beside(live, ".partial"));
  const auto old = Beside(live, ".old");
  if (!fs::exists(old)) return;
  if (fs::exists(live)) fs::remove_all(old);
  else fs::rename(old, live);
}
void SwapIn(const fs::path& fresh, const fs::path& live) {
  const auto here = Beside(live, ".partial"), old = Beside(live, ".old");
  fs::remove_all(here);
  fs::remove_all(old);
  fs::create_directories(live.parent_path());
  std::error_code ec;
  fs::rename(fresh, here, ec);
  if (ec) fs::copy(fresh, here, fs::copy_options::recursive);  // a library or user_data on another drive
  if (fs::exists(live)) fs::rename(live, old);
  fs::rename(here, live);
  fs::remove_all(old);
}

// Where PrepareContent puts the DLC.
fs::path DlcDirectory(const fs::path& library) {
  return UserDataRoot(library) / "0000000000000000" / "5841096A" / "00000002" / "Pack01";
}

// The DLC, unpacked into the stage: returns the folder that holds Pack01.apf.
fs::path UnpackDlc(const fs::path& source, const fs::path& stage) {
  const auto unpacked = stage / "dlc";
  if (fs::is_directory(source)) {
    fs::copy(source, unpacked, fs::copy_options::recursive);
  } else {
    const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(source);
    Require(bool(header), "The DLC you picked isn't an Xbox 360 package. Pick the DLC package, or its folder with Pack01.apf.");
    Require(uint32_t(header->metadata.execution_info.title_id) == 0x5841096A,
            "The DLC you picked is for another game. Pick the Hydro Thunder Hurricane DLC.");
    Require(ExtractPackageTo(source, unpacked), "The DLC package couldn't be unpacked. It may be incomplete: copy it again.");
  }
  if (fs::is_regular_file(unpacked / "Pack01.apf")) return unpacked;
  for (const auto& entry : fs::recursive_directory_iterator(unpacked))
    if (entry.is_regular_file() && entry.path().filename() == "Pack01.apf") return entry.path().parent_path();
  throw Failure("The DLC you picked has no Pack01.apf. Pick the Hydro Thunder Hurricane DLC package.");
}

// The game's default settings, as it copies them itself on its first start.
void CopyDefaultSettings(const fs::path& settings) {
  const auto config = settings / "hydrothunder.toml";
  const auto shipped = rex::filesystem::GetExecutableFolder() / "hydrothunder.toml";
  if (!fs::exists(config) && fs::exists(shipped)) fs::copy_file(shipped, config);
}

int Prepare(const fs::path& game, const fs::path& update, const fs::path& dlc) {
  Require(!LauncherDataDirectory().empty() || std::getenv("HT_SETTINGS_ROOT"),
          "--prepare is RecompLauncher's setup step: it needs RECOMPLAUNCHER_DATA, the game's data folder.");
  // The same folders the game uses when it starts with this environment.
  const auto settings = SettingsDirectory();
  fs::create_directories(settings);
  Lock lock(settings / "hydrothunder-prepare.lock");
  const auto library = LibraryRoot(settings / "library");
  const auto stage = settings / "prepare.partial";
  const auto marker_file = library / "hydrothunder-prepared.txt";
  Progress(0, "Checking your files");

  Recover(library / "game_data");
  Recover(library / "imports");
  Recover(DlcDirectory(library));
  fs::remove_all(stage);
  struct Cleanup {
    fs::path stage;
    ~Cleanup() { std::error_code ec; fs::remove_all(stage, ec); }
  } cleanup{stage};

  for (const auto* picked : {&game, &update, &dlc})
    Require(!Given(*picked) || (!saves::Within(*picked, settings) && !saves::Within(*picked, library)),
            "Pick your own copy of the game, not a file inside the game's folder.");

  Marker marker = ReadMarker(marker_file);
  const auto used = [&marker](const char* key) {
    const auto found = marker.find(key);
    return found == marker.end() ? std::string() : found->second;
  };
  const bool ready = ReadyForLaunch(library);
  if (!ready)
    Require(Given(game), game.empty()
        ? "Pick your copy of Hydro Thunder Hurricane: its Xbox 360 package, or the folder with default.xex and Base.apf."
        : "Your copy of Hydro Thunder Hurricane isn't where it was. Pick it again.");
  const bool new_game = Given(game) && (!ready || used("game") != Identity(game));
  const bool new_dlc = Given(dlc) && (used("dlc") != Identity(dlc) ||
                                      !fs::is_regular_file(DlcDirectory(library) / "Pack01.apf"));
  if (!new_game && !new_dlc) {
    CopyDefaultSettings(settings);
    if (!fs::exists(marker_file)) WriteMarker(marker_file, marker);
    Progress(1, "Already set up");
    return 0;
  }

  // From here until the end, the game isn't marked as set up.
  fs::remove(marker_file);
  if (new_game) {
    fs::create_directories(stage);
    fs::path title_update;
    const auto kept = library / "imports" / "update" / "default.xexp";
    if (Given(update)) {
      title_update = update;
    } else if (fs::is_regular_file(kept)) {
      // Kept from the last setup: the same title update, 0.0.2.1.
      fs::create_directories(stage / "imports" / "update");
      fs::copy_file(kept, stage / "imports" / "update" / "default.xexp");
    }
    try {
      PrepareContent(stage, {game, fs::path{}, title_update}, ImportProgress);
    } catch (const std::exception&) {
      // The game was read but no update came with it: a game folder that is already on
      // 0.0.2.1 needs none, any other copy does.
      if (fs::exists(stage / "game_data" / "default.xex") &&
          !fs::exists(stage / "imports" / "update" / "default.xexp"))
        throw Failure(title_update.empty()
            ? "Pick the title update 0.0.2.1 too: its UPDATE package, or default.xexp."
            : "The title update you picked isn't Hydro Thunder Hurricane's. Pick its UPDATE package, or default.xexp.");
      throw;
    }
    Progress(0.90, "Putting the game in place");
    SwapIn(stage / "game_data", library / "game_data");
    SwapIn(stage / "imports", library / "imports");
    fs::remove_all(stage);
    marker["game"] = Identity(game);
    if (!title_update.empty()) marker["update"] = Identity(title_update);
  }
  if (new_dlc) {
    Progress(new_game ? 0.92 : 0.10, "Unpacking the DLC");
    fs::create_directories(stage);
    SwapIn(UnpackDlc(dlc, stage), DlcDirectory(library));
    fs::remove_all(stage);
    marker["dlc"] = Identity(dlc);
  }
  Require(ReadyForLaunch(library), "The game files aren't complete. Pick your game package and title update 0.0.2.1 again.");
  CopyDefaultSettings(settings);
  WriteMarker(marker_file, marker);
  Progress(1, "Ready to play");
  return 0;
}

}  // namespace

int Run(const fs::path& game, const fs::path& update, const fs::path& dlc) {
  try {
    return Prepare(game, update, dlc);
  } catch (const Failure& failure) {
    std::fflush(stdout);
    std::fprintf(stderr, "%s\n", failure.what());
  } catch (const std::exception& error) {
    // PrepareContent's own reasons (in Portuguese), then one for the player.
    std::fflush(stdout);
    std::fprintf(stderr, "%s\n", error.what());
    std::fprintf(stderr, "Hydro Thunder Hurricane couldn't be set up from these files. Check they are the game "
                         "package and title update 0.0.2.1, and that the drive has free space, then set up again.\n");
  }
  std::fflush(stderr);
  return 1;
}

}  // namespace ht::prepare
