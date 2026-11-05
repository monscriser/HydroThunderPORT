#pragma once
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <string>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace ht {
inline std::filesystem::path SettingsDirectory(){
  if(const auto* p=std::getenv("HT_SETTINGS_ROOT"))return std::filesystem::u8path(p);
#if defined(_WIN32)
  if(const auto* p=std::getenv("APPDATA"))return std::filesystem::u8path(p)/"HydroThunderRecompiled";
#else
  if(const auto* p=std::getenv("XDG_DATA_HOME"))return std::filesystem::u8path(p)/"HydroThunderRecompiled";
  if(const auto* p=std::getenv("HOME"))return std::filesystem::u8path(p)/".local/share/HydroThunderRecompiled";
#endif
  throw std::runtime_error("Nao foi possivel encontrar a pasta de configuracao do usuario.");
}
inline std::filesystem::path GameDirectory(const std::filesystem::path& root){
  if(std::filesystem::is_regular_file(root/"game_data/Base.apf"))return root/"game_data";
  return std::filesystem::is_regular_file(root/"Base.apf") ? root : root/"game_data";
}
inline std::filesystem::path LibraryRoot(const std::filesystem::path& fallback){
  // Existing integration tests stay completely isolated from the user's registry.
  if(const char* test=std::getenv("HT_BOOT_TEST");test && std::string(test)=="1")return fallback;
  std::ifstream in(SettingsDirectory()/"library-location.txt",std::ios::binary);
  std::string version,path;std::getline(in,version);in>>std::quoted(path);
  if(version=="HTH-LIBRARY-1" && !path.empty())return std::filesystem::u8path(path);
  return fallback;
}
inline std::filesystem::path ShaderCacheDirectory(const std::filesystem::path& root){
#if defined(_WIN32)
  const auto existing=root/"cache_windows";
#else
  const auto existing=root/"cache_linux";
#endif
  // Reuse a compatible installed cache without copying it on launcher updates.
  return std::filesystem::is_directory(existing) ? existing : SettingsDirectory()/"shader_cache";
}
// user_data (saves, DLC, achievements). Default: next to the game, <library>/user_data.
// The player may move the whole folder; the choice lives in the settings folder, so the
// game, the launcher, the Steam shortcut and the AppImage all read the same place.
inline std::filesystem::path CustomUserDataRoot(){
  std::ifstream in(SettingsDirectory()/"user-data-location.txt",std::ios::binary);
  std::string version,path;std::getline(in,version);in>>std::quoted(path);
  if(version=="HTH-USERDATA-1" && !path.empty())return std::filesystem::u8path(path);
  return {};
}
inline std::filesystem::path DefaultUserDataRoot(const std::filesystem::path& library_root){
  return library_root/"user_data";
}
inline std::filesystem::path UserDataRoot(const std::filesystem::path& library_root){
  auto custom=CustomUserDataRoot();
  return custom.empty() ? DefaultUserDataRoot(library_root) : custom;
}
// Empty path = back to the default next to the game.
inline void RememberUserDataRoot(const std::filesystem::path& folder){
  auto directory=SettingsDirectory();std::filesystem::create_directories(directory);
  auto file=directory/"user-data-location.txt",temp=directory/"user-data-location.new";
  std::string text;
  if(!folder.empty()){auto u8=std::filesystem::weakly_canonical(folder).u8string();text.assign(reinterpret_cast<const char*>(u8.data()),u8.size());}
  {std::ofstream out(temp,std::ios::binary);out<<"HTH-USERDATA-1\n"<<std::quoted(text)<<"\n";out.close();if(!out)throw std::runtime_error("Nao foi possivel salvar a pasta de dados.");}
#if defined(_WIN32)
  if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Nao foi possivel salvar a pasta de dados.");
#else
  std::filesystem::rename(temp,file);
#endif
}
inline void RememberLibrary(const std::filesystem::path& root){
  if(!std::filesystem::is_directory(root))throw std::runtime_error("A pasta do jogo nao existe.");
  auto directory=SettingsDirectory();std::filesystem::create_directories(directory);
  auto file=directory/"library-location.txt",temp=directory/"library-location.new";
  auto path=std::filesystem::weakly_canonical(root).u8string();
  {std::ofstream out(temp,std::ios::binary);out<<"HTH-LIBRARY-1\n"<<std::quoted(std::string(reinterpret_cast<const char*>(path.data()),path.size()))<<"\n";out.close();if(!out)throw std::runtime_error("Nao foi possivel lembrar a pasta do jogo.");}
#if defined(_WIN32)
  if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Nao foi possivel salvar a pasta do jogo.");
#else
  std::filesystem::rename(temp,file);
#endif
}
}
