#pragma once
#include <filesystem>
#include <string>
#include <stdexcept>
#include <cstdint>
namespace ht {
inline size_t ClearShaderCache(const std::filesystem::path& directory){
  const auto name=directory.filename().string();
  if(name!="shader_cache" && name!="cache_windows" && name!="cache_linux")
    throw std::runtime_error("A pasta de cache nao foi reconhecida. Nada foi removido.");
  if(!std::filesystem::exists(directory))return 0;
  if(std::filesystem::is_symlink(std::filesystem::symlink_status(directory)))
    throw std::runtime_error("A pasta de cache simbolica nao pode ser limpa automaticamente.");
  size_t count=0;
  for(const auto& entry:std::filesystem::recursive_directory_iterator(directory)){
    if(!entry.is_regular_file() || entry.is_symlink())continue;
    const auto file=entry.path().filename().string();
    if(file.starts_with("5841096A") && (file.ends_with(".xsh") || file.ends_with(".xpso") || file.ends_with(".vk.bin") || file.ends_with(".vk.bin.tmp")))
      count+=std::filesystem::remove(entry.path()) ? 1 : 0;
  }
  return count;
}
}
