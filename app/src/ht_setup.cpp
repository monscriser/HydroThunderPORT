#include "ht_setup.h"

#include <cstdio>
#include <string>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/logging.h>
#include <rex/system.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace ht {
namespace {

// X_STATUS_SUCCESS is a macro that expands to a cast - ((X_STATUS)0) - so the
// type has to be visible unqualified for it to compile at all. Qualifying our
// own declarations is not enough.
using rex::X_STATUS;

// Hardcoded because this is a single-title port and the import happens before
// the kernel exists, so there is nothing to ask.
constexpr const char* kTitleId = "5841096A";
constexpr const char* kDlcContentType = "00000002";  // XContentType::kMarketplaceContent
constexpr const char* kDlcXuid = "0000000000000000";

// Read in chunks rather than whole files: the game package is ~460 MB and some
// entries inside it are large.
constexpr size_t kCopyChunk = 1u << 20;

void Info(const std::string& message) {
  rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Help, message);
}

void Error(const std::string& message) {
  rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error, message);
}

#if defined(_WIN32)

bool AskYesNo(const std::string& message, const std::string& title) {
  const int result = ::MessageBoxA(nullptr, message.c_str(), title.c_str(),
                                   MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
  return result == IDYES;
}

// A plain "pick a file" dialog. The packages have no extension - their name is
// a content id - so the filter has to allow everything.
bool PickFile(const std::string& title, std::filesystem::path* out_path) {
  wchar_t buffer[MAX_PATH * 4] = {};
  std::wstring wide_title(title.begin(), title.end());

  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = nullptr;
  ofn.lpstrFilter = L"Game package (any file)\0*.*\0";
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = DWORD(std::size(buffer));
  ofn.lpstrTitle = wide_title.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

  if (!::GetOpenFileNameW(&ofn)) {
    return false;  // cancelled, or the dialog failed
  }
  *out_path = std::filesystem::path(buffer);
  return true;
}

#else

bool AskYesNo(const std::string&, const std::string&) { return false; }
bool PickFile(const std::string&, std::filesystem::path*) { return false; }

#endif  // _WIN32

// Writes one entry out of the package. Returns false on the first real error -
// a partial extraction is worse than none, because the missing-file check
// would then pass while the install is incomplete.
bool WriteEntry(rex::filesystem::Entry* entry, const std::filesystem::path& dest) {
  rex::filesystem::File* file = nullptr;
  if (entry->Open(rex::filesystem::FileAccess::kGenericRead, &file) != X_STATUS_SUCCESS || !file) {
    REXLOG_ERROR("[ht-setup] could not open '{}' inside the package", entry->name());
    return false;
  }

  std::FILE* out = std::fopen(dest.string().c_str(), "wb");
  if (!out) {
    REXLOG_ERROR("[ht-setup] could not create '{}'", dest.string());
    file->Destroy();
    return false;
  }

  std::vector<uint8_t> chunk(kCopyChunk);
  size_t offset = 0;
  const size_t total = entry->size();
  bool ok = true;
  while (offset < total) {
    const size_t want = (total - offset) < kCopyChunk ? (total - offset) : kCopyChunk;
    size_t got = 0;
    if (file->ReadSync(std::span<uint8_t>(chunk.data(), want), offset, &got) != X_STATUS_SUCCESS ||
        got == 0) {
      REXLOG_ERROR("[ht-setup] read failed in '{}' at offset {}", entry->name(), offset);
      ok = false;
      break;
    }
    if (std::fwrite(chunk.data(), 1, got, out) != got) {
      REXLOG_ERROR("[ht-setup] write failed for '{}' - is the disk full?", dest.string());
      ok = false;
      break;
    }
    offset += got;
  }

  std::fclose(out);
  file->Destroy();
  return ok;
}

bool ExtractTree(rex::filesystem::Entry* entry, const std::filesystem::path& dest, int* files,
                 const std::string& prefix) {
  std::error_code ec;
  for (const auto& child : entry->children()) {
    const std::filesystem::path leaf(child->name());
    if (leaf.empty() || leaf.is_absolute() || leaf.has_parent_path() || leaf == "." || leaf == "..") {
      REXLOG_ERROR("[ht-setup] invalid package entry name");
      return false;
    }
    const auto target = dest / child->name();
    const std::string rel = prefix.empty() ? child->name() : prefix + "/" + child->name();
    if (child->attributes() & rex::filesystem::kFileAttributeDirectory) {
      REXLOG_INFO("[ht-setup]   dir  {}", rel);
      std::filesystem::create_directories(target, ec);
      if (!ExtractTree(child.get(), target, files, rel)) {
        return false;
      }
    } else {
      // Logged per file: the package's own directory table is the only way to
      // settle questions about odd layouts, and this is the SDK's parse of it
      // rather than a guess.
      REXLOG_INFO("[ht-setup]   file {} ({} bytes)", rel, child->size());
      std::filesystem::create_directories(dest, ec);
      if (!WriteEntry(child.get(), target)) {
        return false;
      }
      ++(*files);
    }
  }
  return true;
}

// Repairs entries whose names arrive with the parent directory glued on.
//
// The DLC package's directory table contains BOTH of these, as six separate
// entries (confirmed by logging the SDK's own parse of the table):
//
//     Audio/Xbox360/Pack01Track01.fsb            16 bytes
//     Audio/Xbox360Pack01Track01.fsb     11,267,136 bytes
//
// The flattened name is exactly the directory name concatenated with the file
// name, and the real audio only exists under that flattened entry while a
// 16-byte stub sits at the proper path. Extracted as-is, the game reads the
// stub and dies the moment you press Start; moving the large file onto the
// stub makes it work. Both directions were verified on the real DLC.
//
// Whether the console resolves those stubs some other way, or the packer wrote
// the table oddly, is not established - but the repair is narrow (it only
// fires when a real file's name is precisely <dir><file> beside that dir, and
// only when it is bigger than what it replaces) and the result is verified to
// run.
int RepairFlattenedNames(const std::filesystem::path& root) {
  std::error_code ec;
  int repaired = 0;
  for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
       it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (ec || !it->is_directory(ec)) {
      continue;
    }
    const auto dir = it->path();
    const auto parent = dir.parent_path();
    const auto dir_name = dir.filename().string();

    for (auto f = std::filesystem::directory_iterator(dir, ec);
         f != std::filesystem::directory_iterator(); f.increment(ec)) {
      if (ec || !f->is_regular_file(ec)) {
        continue;
      }
      const auto nested = f->path();
      const auto flattened = parent / (dir_name + nested.filename().string());
      if (!std::filesystem::exists(flattened, ec)) {
        continue;
      }
      const auto flat_size = std::filesystem::file_size(flattened, ec);
      const auto nested_size = std::filesystem::file_size(nested, ec);
      if (ec || flat_size <= nested_size) {
        continue;
      }
      std::filesystem::remove(nested, ec);
      std::filesystem::rename(flattened, nested, ec);
      if (!ec) {
        REXLOG_INFO("[ht-setup]   repaired {} ({} bytes, was {})",
                    nested.filename().string(), flat_size, nested_size);
        ++repaired;
      }
    }
  }
  return repaired;
}

// Reads a XEX's encryption and compression flags.
//
// This matters more than it looks. A package straight off the marketplace
// holds an ENCRYPTED, LZX-compressed default.xex, and while that loads far
// enough to show the title screen, the game dies the moment it actually needs
// the image - measured: identical binaries and config, only the xex differing,
// crash on START versus a healthy 991 MB process.
//
// So a raw retail xex has to be reported, not silently accepted. An install
// that boots and then dies is far worse than one that refuses up front and
// says why.
//
// Layout: 'XEX2', then at 0x14 the optional header count, then that many
// (key, value) pairs from 0x18. The key whose top 24 bits are 0x000003 is the
// file-format info, and its value is a file offset holding, at +4, a u16
// encryption type and a u16 compression type.
bool ReadXexFormat(const std::filesystem::path& xex, uint16_t* encryption,
                   uint16_t* compression) {
  std::FILE* f = std::fopen(xex.string().c_str(), "rb");
  if (!f) {
    return false;
  }
  std::vector<uint8_t> head(0x4000);
  const size_t got = std::fread(head.data(), 1, head.size(), f);
  std::fclose(f);
  if (got < 0x100 || std::memcmp(head.data(), "XEX2", 4) != 0) {
    return false;
  }

  auto be32 = [&](size_t off) -> uint32_t {
    return (uint32_t(head[off]) << 24) | (uint32_t(head[off + 1]) << 16) |
           (uint32_t(head[off + 2]) << 8) | uint32_t(head[off + 3]);
  };
  auto be16 = [&](size_t off) -> uint16_t {
    return uint16_t((uint32_t(head[off]) << 8) | head[off + 1]);
  };

  const uint32_t count = be32(0x14);
  for (uint32_t i = 0; i < count && (0x18 + i * 8 + 8) <= got; ++i) {
    const uint32_t key = be32(0x18 + i * 8);
    const uint32_t value = be32(0x18 + i * 8 + 4);
    if ((key >> 8) == 0x000003 && size_t(value) + 8 <= got) {
      *encryption = be16(value + 4);
      *compression = be16(value + 6);
      return true;
    }
  }
  return false;
}

// Creates game_data/DataCache if it is missing.
//
// The game opens 'game:\DataCache\' at startup. The directory is NOT in the
// retail package, and the game: mount cannot create it - the call comes back
// ACCESS_DENIED (0xC0000022) and the guest dies shortly after. When the
// directory already exists the same call returns OBJECT_NAME_COLLISION
// (0xC0000035), which is harmless and is what a working install looks like.
//
// Measured both ways on the same build: absent = dead, present = healthy.
// Runs on every launch, because a hand-assembled game_data will not have it
// either.
void EnsureDataCache(const std::filesystem::path& game_data) {
  const auto cache = game_data / "DataCache";
  std::error_code ec;
  if (std::filesystem::exists(cache, ec)) {
    return;
  }
  if (std::filesystem::create_directories(cache, ec)) {
    REXLOG_INFO("[ht-setup] created missing game_data/DataCache");
  } else {
    REXLOG_WARN("[ht-setup] could not create game_data/DataCache: {}", ec.message());
  }
}

#if defined(_WIN32)

// Looks for XEXTool. Beside the game first, then inside game_data, then PATH.
bool FindXexTool(const std::filesystem::path& exe_dir, std::filesystem::path* out) {
  std::error_code ec;
  const std::filesystem::path candidates[] = {
      exe_dir / "xextool.exe",
      exe_dir / "tools" / "xextool.exe",
      exe_dir / "game_data" / "xextool.exe",
  };
  for (const auto& candidate : candidates) {
    if (std::filesystem::exists(candidate, ec)) {
      *out = candidate;
      return true;
    }
  }
  wchar_t found[MAX_PATH] = {};
  if (::SearchPathW(nullptr, L"xextool.exe", nullptr, MAX_PATH, found, nullptr)) {
    *out = std::filesystem::path(found);
    return true;
  }
  return false;
}

// Runs XEXTool to decrypt and decompress the image in place.
//
// '-c u -e u' is what produces a runnable file: verified by converting a raw
// retail xex and getting a byte-identical match (same MD5) to a known-good
// one. The output goes to a temporary file first, so a failed run cannot
// destroy the original.
bool RunXexTool(const std::filesystem::path& tool, const std::filesystem::path& xex) {
  const auto temp = xex.parent_path() / "default_converted.xex";
  std::error_code ec;
  std::filesystem::remove(temp, ec);

  std::wstring cmd = L"\"" + tool.wstring() + L"\" -c u -e u -o \"" + temp.wstring() +
                     L"\" \"" + xex.wstring() + L"\"";

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi = {};
  std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
  mutable_cmd.push_back(L'\0');

  if (!::CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
    REXLOG_ERROR("[ht-setup] could not run XEXTool");
    return false;
  }
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 1;
  ::GetExitCodeProcess(pi.hProcess, &exit_code);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);

  if (!std::filesystem::exists(temp, ec) || std::filesystem::file_size(temp, ec) == 0) {
    REXLOG_ERROR("[ht-setup] XEXTool exited {} without producing a file", exit_code);
    std::filesystem::remove(temp, ec);
    return false;
  }

  std::filesystem::remove(xex, ec);
  std::filesystem::rename(temp, xex, ec);
  if (ec) {
    REXLOG_ERROR("[ht-setup] could not replace default.xex: {}", ec.message());
    return false;
  }
  REXLOG_INFO("[ht-setup] converted default.xex with XEXTool");
  return true;
}

#else
bool FindXexTool(const std::filesystem::path&, std::filesystem::path*) { return false; }
bool RunXexTool(const std::filesystem::path&, const std::filesystem::path&) { return false; }
#endif  // _WIN32

// Checks default.xex is in a form the game can actually run, and explains the
// fix if it is not.
//
// Runs on EVERY launch, not just after an import. The first version of this
// only checked freshly imported files, so anyone who assembled game_data by
// hand - which the README documents as the supported route for an
// already-extracted copy - still got the silent boot-then-die.
bool ValidateXexUsable(const std::filesystem::path& exe_dir, const std::filesystem::path& xex) {
  uint16_t encryption = 0, compression = 0;
  if (!ReadXexFormat(xex, &encryption, &compression)) {
    return true;  // unreadable header: let the runtime have its say
  }
  if (encryption == 0 && compression != 2) {
    return true;  // already runnable
  }

  REXLOG_WARN("[ht-setup] default.xex is encryption={} compression={}; converting",
              encryption, compression);

  // Convert it automatically if XEXTool is available - the player should not
  // have to run a command line to play a game.
  std::filesystem::path tool;
  if (FindXexTool(exe_dir, &tool)) {
    REXLOG_INFO("[ht-setup] using XEXTool at '{}'", tool.string());
    if (RunXexTool(tool, xex) && ReadXexFormat(xex, &encryption, &compression) &&
        encryption == 0 && compression != 2) {
      Info("Your game executable needed converting before it could run, and\n"
           "that has been done automatically.\n\n"
           "This only happens once.");
      return true;
    }
    Error("XEXTool was found, but converting default.xex did not work.\n\n"
          "See the log next to this program for details. You can convert it\n"
          "yourself with:\n\n"
          "    xextool -c u -e u -o default.xex default.xex\n\n"
          "then put the result in the game_data folder.");
    return false;
  }

  // xextool.exe ships beside the game, so reaching here means the install is
  // incomplete rather than the user having missed a step.
  Error("xextool.exe is missing from this folder.\n\n"
        "It ships with the game and is needed to convert the game executable,\n"
        "which is encrypted and compressed inside a retail package. Without\n"
        "that step the game reaches the title screen and then closes as soon\n"
        "as you press Start.\n\n"
        "Re-extract the download, keeping all of its files together.\n\n"
        "Or convert it yourself with:\n"
        "    xextool -c u -e u -o default.xex default.xex");
  return false;
}

// Mounts an STFS package and unpacks all of it into `dest`.
bool ExtractPackage(const std::filesystem::path& package, const std::filesystem::path& dest, bool show_dialogs = true) {
  auto report_error = [show_dialogs](const std::string& message) {
    if (show_dialogs) Error(message);
    else REXLOG_ERROR("[ht-setup] {}", message);
  };
  // Validate before touching anything, so a wrong pick is a clear message
  // rather than an empty folder.
  if (!rex::filesystem::StfsContainerDevice::ReadPackageHeader(package)) {
    report_error("That file is not an Xbox 360 content package.\n\n"
          "Expected the package file itself - the one whose name is a long\n"
          "string of hex characters, with no file extension.");
    return false;
  }

  rex::filesystem::StfsContainerDevice device("\\SETUP", package);
  if (!device.Initialize()) {
    report_error("That package could not be opened. It may be incomplete or corrupted.");
    return false;
  }

  auto* root = device.ResolvePath("");
  if (!root) {
    report_error("That package opened but appears to be empty.");
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(dest, ec);

  int files = 0;
  if (!ExtractTree(root, dest, &files, "")) {
    report_error("Extracting the package failed part-way through.\n\n"
          "The partly written files were left in place so you can inspect them;\n"
          "delete the folder and try again. See the log for details.");
    return false;
  }
  REXLOG_INFO("[ht-setup] extracted {} file(s) from '{}' to '{}'", files, package.string(),
              dest.string());
  const int repaired = RepairFlattenedNames(dest);
  if (repaired) {
    REXLOG_INFO("[ht-setup] repaired {} file(s) with flattened names", repaired);
  }
  return true;
}

// Kept compiled but not called - see the note at its call site in
// EnsureGameData for why DLC import is disabled for this release.
// Returns true only if DLC was actually installed.
bool ImportOptionalDlc(const std::filesystem::path& exe_dir) {
  if (!AskYesNo("Do you also have the Hydro Thunder Hurricane DLC?\n\n"
                "This is optional - the game runs fine without it. You can add it\n"
                "later by deleting the user_data folder and relaunching.\n\n"
                "Select it now?",
                "Optional DLC")) {
    return false;
  }

  std::filesystem::path package;
  if (!PickFile("Select your Hydro Thunder Hurricane DLC package", &package)) {
    return false;
  }

  // See ht_setup.h: xuid is ZERO for marketplace content, and the package
  // becomes a DIRECTORY named after the container file.
  const auto dest = exe_dir / "user_data" / kDlcXuid / kTitleId / kDlcContentType /
                    package.filename();
  if (!ExtractPackage(package, dest)) {
    return false;
  }
  Info("DLC installed.");
  return true;
}

}  // namespace

bool ExtractPackageTo(const std::filesystem::path& package, const std::filesystem::path& dest) {
  return ExtractPackage(package, dest, false);
}

bool EnsureGameData(const std::filesystem::path& exe_dir) {
  const auto game_data = exe_dir / "game_data";
  const auto xex = game_data / "default.xex";

  std::error_code ec;
  if (std::filesystem::exists(xex, ec)) {
    // Already installed - but still make sure DataCache is there and the
    // executable is runnable. A hand-assembled game_data has neither.
    EnsureDataCache(game_data);
    if (!ValidateXexUsable(exe_dir, xex)) {
      return false;
    }
    // DLC import would go here, and is deliberately not offered - see the
    // note on ImportOptionalDlc. The plumbing to add DLC to an existing
    // install without reinstalling is written and works; only the crash it
    // causes is unresolved.
    return true;
  }

  Info("Hydro Thunder Hurricane: Recompiled\n\n"
       "This port does not include any game content, so it needs the files\n"
       "from your own copy of the game.\n\n"
       "On the next screen, select your Hydro Thunder Hurricane package - the\n"
       "file whose name is a long string of hex characters, with no extension.\n\n"
       "It will be unpacked next to this program. This only happens once.");

  std::filesystem::path package;
  if (!PickFile("Select your Hydro Thunder Hurricane game package", &package)) {
    Error("No game files selected, so the game cannot start.\n\n"
          "Run it again when you have your copy to hand.");
    return false;
  }

  if (!ExtractPackage(package, game_data)) {
    return false;
  }

  // Prove the import actually produced what the runtime needs, rather than
  // trusting that it succeeded.
  if (!std::filesystem::exists(xex, ec)) {
    Error("That package unpacked, but it does not contain default.xex, so it\n"
          "is not Hydro Thunder Hurricane.\n\n"
          "Delete the game_data folder next to this program and try again with\n"
          "the correct package.");
    return false;
  }

  // The game creates neither of these for itself.
  EnsureDataCache(game_data);
  if (!ValidateXexUsable(exe_dir, xex)) {
    return false;
  }

  // DLC import is DISABLED for this release, deliberately.
  //
  // The importer unpacks the DLC without error, but pressing Start then
  // crashes the game MOST of the time - and the "most" is the important part.
  // Measured over repeated launches from an identical install:
  //
  //     DLC present : CRASHED, ALIVE, CRASHED   (plus the original report)
  //     DLC absent  : ALIVE,   ALIVE, ALIVE
  //
  // So the DLC does cause it, but probabilistically. Single-run A/B tests are
  // therefore worthless here: three separate conclusions were drawn from one
  // lucky launch each before this was noticed. Anything claimed about this
  // crash needs a repeated trial, not one observation.
  //
  // RepairFlattenedNames (above) fixes a genuine defect in what we write - the
  // package yields 16-byte stubs at Audio/Xbox360/*.fsb alongside the real
  // audio under flattened names, confirmed by logging the SDK's own parse of
  // the directory table - but repairing it did NOT stop the crash. So the
  // layout is not the cause, and the real cause is still unknown.
  //
  // Shipping a one-click button that bricks the game is far worse than not
  // shipping it, so the prompt stays off until that is understood. The code
  // is kept and working for when it is:
  //
  //   ImportOptionalDlc(exe_dir);

  Info("Setup complete. The game will start now.");
  return true;
}

}  // namespace ht
