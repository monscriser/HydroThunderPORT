#include "ht_content_install.h"
#include "ht_library_paths.h"
#include "ht_setup.h"
#include <fstream>
#include <rex/filesystem.h>
#include <chrono>
#include <stdexcept>
#include <vector>
#include <rex/filesystem/devices/stfs_container_device.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif
namespace ht {
namespace {
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
uint32_t EntryPoint(const fs::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::array<unsigned char, 0x4000> bytes{};
  file.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  const auto size = size_t(file.gcount());
  if (size < 0x18 || std::string(reinterpret_cast<char*>(bytes.data()), 4) != "XEX2") return 0;
  auto be = [&](size_t off) { return uint32_t(bytes[off]) << 24 | uint32_t(bytes[off+1]) << 16 |
                                   uint32_t(bytes[off+2]) << 8 | bytes[off+3]; };
  for (size_t i = 0; i < be(0x14) && 0x20 + i*8 <= size; ++i)
    if (be(0x18+i*8) == 0x00010100) return be(0x1C+i*8);
  return 0;
}
void CopyTree(const fs::path& source, const fs::path& dest) {
  Require(fs::is_directory(source), "A pasta selecionada nao existe.");
  const auto from = fs::weakly_canonical(source);
  const auto to = fs::weakly_canonical(dest);
  Require(from != to, "Escolha a pasta original, fora da pasta de destino.");
  const auto relative = to.lexically_relative(from);
  Require(relative.empty() || *relative.begin() == "..", "O destino nao pode estar dentro da pasta original.");
  fs::create_directories(dest);
  for (auto& entry : fs::recursive_directory_iterator(source)) {
    Require(!fs::is_symlink(entry.symlink_status()), "Use arquivos originais copiados, sem atalhos simbolicos.");
    const auto target = dest / entry.path().lexically_relative(source);
    if (entry.is_directory()) fs::create_directories(target);
    else if (entry.is_regular_file()) {
      fs::create_directories(target.parent_path());
      fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
    }
  }
}
fs::path FindFile(const fs::path& dir, const char* name) {
  if (fs::is_regular_file(dir / name)) return dir / name;
  if (fs::is_directory(dir)) for (auto& entry : fs::recursive_directory_iterator(dir))
    if (entry.is_regular_file() && entry.path().filename() == name) return entry.path();
  return {};
}
void Unpack(const fs::path& source, const fs::path& dest) {
  if (fs::is_directory(source)) { CopyTree(source, dest); return; }
  const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(source);
  Require(bool(header), "Selecione uma pasta extraida ou um pacote Xbox 360 valido.");
  Require(uint32_t(header->metadata.execution_info.title_id) == 0x5841096A,
          "Esse pacote nao pertence a Hydro Thunder Hurricane.");
  Require(ExtractPackageTo(source, dest), "Nao foi possivel extrair o pacote. Consulte o registro.");
}
int RunTool(const fs::path& tool, const std::vector<std::string>& args) {
#if defined(_WIN32)
  auto wide = [](const std::string& text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
    return result;
  };
  auto quote = [](const std::wstring& arg) {
    std::wstring result=L"\""; size_t slashes=0;
    for (wchar_t c : arg) {
      if (c == L'\\') { ++slashes; continue; }
      result.append(c == L'\"' ? slashes*2+1 : slashes, L'\\'); slashes=0;
      result += c;
    }
    result.append(slashes*2, L'\\'); result += L'\"'; return result;
  };
  std::wstring command = quote(tool.wstring());
  for (const auto& arg : args) command += L" " + quote(wide(arg));
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(tool.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return -1;
  WaitForSingleObject(process.hProcess, INFINITE);
  DWORD exit_code=1; GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread); CloseHandle(process.hProcess); return int(exit_code);
#else
  std::vector<std::string> strings{tool.string()}; strings.insert(strings.end(), args.begin(), args.end());
  std::vector<char*> argv; for (auto& s : strings) argv.push_back(s.data()); argv.push_back(nullptr);
  pid_t pid; if (posix_spawn(&pid, tool.c_str(), nullptr, nullptr, argv.data(), environ)) return -1;
  int status=0; if (waitpid(pid, &status, 0) < 0) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}
}
bool ReadyForLaunch(const fs::path& root) {
  return fs::is_regular_file(GameDirectory(root) / "Base.apf") &&
         EntryPoint(GameDirectory(root) / "default.xex") == 0x823EF338;
}
void PrepareContent(const fs::path& root, const std::array<fs::path, 3>& sources,
                    const std::function<void(std::string)>& progress) {
  // The selected original directory may itself be the persistent library root.
  // Keep the snapshot outside that directory, so importing it never recursively
  // copies its own destination. Only this newly-created private directory is
  // removed when the import finishes or fails.
  const auto staging = fs::temp_directory_path() / "HydroThunderRecompiled-import";
  fs::create_directories(staging);
  auto unique_stage = staging / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  Require(fs::create_directory(unique_stage), "Nao foi possivel criar uma pasta temporaria exclusiva.");
  struct StageCleanup {
    fs::path path;
    ~StageCleanup() { std::error_code error; fs::remove_all(path, error); }
  } cleanup{unique_stage};
  const auto game = root / "game_data";
  const auto original = root / "imports" / "jogo" / "default-original.xex";
  if (!sources[0].empty()) {
    progress("1/3 - Importando os arquivos do jogo...");
    auto source = sources[0];
    if (source.filename() == "default.xex") source = source.parent_path();
    Unpack(source, unique_stage / "jogo");
    const auto xex = FindFile(unique_stage / "jogo", "default.xex");
    Require(!xex.empty(), "O jogo precisa conter default.xex.");
    Require(fs::exists(xex.parent_path() / "Base.apf"), "A pasta do jogo esta incompleta: falta Base.apf.");
    fs::create_directories(original.parent_path());
    fs::copy_file(xex, original, fs::copy_options::overwrite_existing);
    CopyTree(xex.parent_path(), game);
  }
  if (!sources[1].empty()) {
    progress("2/3 - Importando a DLC...");
    Unpack(sources[1], unique_stage / "dlc");
    const auto apf = FindFile(unique_stage / "dlc", "Pack01.apf");
    Require(!apf.empty(), "O pacote de DLC precisa conter Pack01.apf.");
    CopyTree(apf.parent_path(), root / "user_data" / "0000000000000000" / "5841096A" / "00000002" / "Pack01");
  }
  auto input = game / "default.xex";
  const auto patch_root = root / "imports" / "update";
  fs::create_directories(patch_root);
  auto patch = patch_root / "default.xexp";
  if (!sources[2].empty()) {
    progress("3/3 - Importando o update...");
    auto source = sources[2];
    fs::path found;
    if (source.filename() == "default.xexp") found = source;
    else { Unpack(source, unique_stage / "update"); found = FindFile(unique_stage / "update", "default.xexp"); }
    Require(!found.empty(), "O pacote de update precisa conter default.xexp.");
    fs::copy_file(found, patch, fs::copy_options::overwrite_existing);
  }
  if (!ReadyForLaunch(root) || !sources[2].empty()) {
    progress("Preparando a versao completa com o update...");
    Require(fs::exists(original), "Importe primeiro o jogo original.");
    Require(fs::exists(patch), "Selecione o update. Esta build usa a versao 0.0.2.1.");
#if defined(_WIN32)
    std::wstring executable_path(32768, L'\0');
    const auto executable_length = GetModuleFileNameW(nullptr, executable_path.data(),
                                                      DWORD(executable_path.size()));
    Require(executable_length && executable_length < executable_path.size(),
            "Nao foi possivel localizar a ferramenta de preparacao.");
    executable_path.resize(executable_length);
    const auto tool = fs::path(executable_path).parent_path() / "xextool-re.exe";
#else
    const auto tool = rex::filesystem::GetExecutableFolder() / "xextool-re";
#endif
    Require(fs::exists(tool), "A ferramenta de importacao esta ausente. Mantenha os arquivos da build juntos.");
    const auto updated = unique_stage / "updated.xex";
    const auto decrypted = unique_stage / "decrypted.xex";
    const auto prepared = unique_stage / "default.xex";
    Require(RunTool(tool, {"patch", rex::path_to_utf8(original), rex::path_to_utf8(patch), rex::path_to_utf8(updated)}) == 0,
            "O update nao corresponde ao jogo selecionado.");
    Require(RunTool(tool, {"decrypt", rex::path_to_utf8(updated), rex::path_to_utf8(decrypted)}) == 0,
            "Nao foi possivel preparar o executavel.");
    Require(RunTool(tool, {"decompress", rex::path_to_utf8(decrypted), rex::path_to_utf8(prepared)}) == 0,
            "Nao foi possivel preparar os arquivos do jogo.");
    Require(EntryPoint(prepared) == 0x823EF338, "Esse update nao corresponde a versao suportada por esta build.");
    fs::copy_file(prepared, input, fs::copy_options::overwrite_existing);
  }
  fs::create_directories(game / "DataCache");
  Require(ReadyForLaunch(root), "Ainda faltam o jogo ou o update compativel.");
  progress("Arquivos preparados. A versao completa esta pronta para iniciar.");
}
}
