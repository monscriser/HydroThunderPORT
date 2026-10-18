#pragma once
#include <filesystem>
#include <fstream>
#include <chrono>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cwctype>
namespace ht::saves {
namespace fs = std::filesystem;
inline bool ProfileName(const std::string& s) {
  return s.size()==16 && std::all_of(s.begin(),s.end(),[](unsigned char c){
    return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');
  });
}
inline bool Within(const fs::path& path,const fs::path& root) {
  auto p=fs::weakly_canonical(path),r=fs::weakly_canonical(root);
  auto pi=p.begin(); for(auto ri=r.begin();ri!=r.end();++ri,++pi){
    if(pi==p.end())return false;
#if defined(_WIN32)
    auto a=pi->wstring(),b=ri->wstring();
    std::transform(a.begin(),a.end(),a.begin(),[](wchar_t c){return std::towlower(c);});
    std::transform(b.begin(),b.end(),b.begin(),[](wchar_t c){return std::towlower(c);});
    if(a!=b)return false;
#else
    if(*pi!=*ri)return false;
#endif
  }
  return true;
}
inline size_t CopySaves(const fs::path& source,const fs::path& target) {
  size_t count=0;
  if(!fs::is_directory(source))return count;
  for(const auto& profile:fs::directory_iterator(source)) {
    if(profile.is_symlink()||!profile.is_directory()||!ProfileName(profile.path().filename().string()))continue;
    const auto saves=profile.path()/"5841096A"/"00000001";
    if(!fs::is_directory(saves))continue;
    for(const auto& item:fs::recursive_directory_iterator(saves)) {
      if(item.is_symlink()||!Within(item.path(),source))throw std::runtime_error("O backup contem um caminho invalido.");
      if(!item.is_regular_file())continue;
      const auto dest=target/fs::relative(item.path(),source);
      if(fs::is_symlink(dest)||!Within(dest,target))throw std::runtime_error("A pasta de saves contem um caminho invalido.");
      fs::create_directories(dest.parent_path());
      fs::copy_file(item.path(),dest,fs::copy_options::overwrite_existing);
      ++count;
    }
  }
  return count;
}
inline fs::path Export(const fs::path& root,const fs::path& destination) {
  const auto source=root/"user_data";
  if(Within(destination,source))throw std::runtime_error("Escolha uma pasta fora de user_data para guardar o backup.");
  const auto stamp=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto backup=destination/("Hydro-Thunder-saves-"+std::to_string(stamp));
  fs::create_directories(backup);
  if(!CopySaves(source,backup/"user_data"))throw std::runtime_error("Nenhum save encontrado. Jogue e salve antes de exportar.");
  std::ofstream marker(backup/"Hydro-Thunder-backup.txt");marker<<"HTH-save-backup-v1\n";marker.close();
  if(!marker)throw std::runtime_error("Nao foi possivel finalizar o backup.");
  return backup;
}
inline void Import(const fs::path& root,const fs::path& backup) {
  std::ifstream marker(backup/"Hydro-Thunder-backup.txt");std::string version;std::getline(marker,version);
  if(version!="HTH-save-backup-v1"||!fs::is_directory(backup/"user_data"))
    throw std::runtime_error("Selecione a pasta Hydro-Thunder-saves criada ao exportar os saves.");
  // Validate and stage everything before touching current progress.
  const auto stage=root/"save_backups"/("restore-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if(!CopySaves(backup/"user_data",stage))throw std::runtime_error("O backup nao contem saves deste jogo.");
  bool has_current=false;
  if(fs::is_directory(root/"user_data"))for(const auto& profile:fs::directory_iterator(root/"user_data")) {
    const auto current=profile.path()/"5841096A"/"00000001";
    if(profile.is_directory()&&ProfileName(profile.path().filename().string())&&fs::is_directory(current))
      for(const auto& file:fs::recursive_directory_iterator(current))if(file.is_regular_file()){has_current=true;break;}
  }
  if(has_current)Export(root,root/"save_backups");
  CopySaves(stage,root/"user_data");
}
}
