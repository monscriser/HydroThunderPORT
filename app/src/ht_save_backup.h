#pragma once
#include <filesystem>
#include <fstream>
#include <chrono>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cwctype>
#include <cctype>
#include "ht_library_paths.h"
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
  const auto source=UserDataRoot(root);
  if(Within(destination,source))throw std::runtime_error("Escolha uma pasta fora de user_data para guardar o backup.");
  const auto stamp=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  const auto backup=destination/("Hydro-Thunder-saves-"+std::to_string(stamp));
  fs::create_directories(backup);
  if(!CopySaves(source,backup/"user_data"))throw std::runtime_error("Nenhum save encontrado. Jogue e salve antes de exportar.");
  std::ofstream marker(backup/"Hydro-Thunder-backup.txt");marker<<"HTH-save-backup-v1\n";marker.close();
  if(!marker)throw std::runtime_error("Nao foi possivel finalizar o backup.");
  return backup;
}
// Xenia keeps the save as <xenia>/content/[<profile>/]5841096A/00000001/ht2save.dat/ht2save.dat
// (header in .../5841096A/Headers/00000001/ht2save.dat.header): same game, same file. The player
// may pick the Xenia folder, its content folder, any folder above the save, or the file itself.
inline fs::path FindXeniaSave(const fs::path& picked) {
  auto lower=[](std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;};
  std::error_code ec;
  if(fs::is_regular_file(picked,ec))return lower(picked.filename().string())=="ht2save.dat" ? picked : fs::path{};
  fs::path best;fs::file_time_type best_time{};bool best_title=false;size_t seen=0;
  for(auto it=fs::recursive_directory_iterator(picked,fs::directory_options::skip_permission_denied,ec);
      it!=fs::recursive_directory_iterator();it.increment(ec)) {
    if(ec||++seen>300000)break;
    if(it.depth()>10){it.disable_recursion_pending();continue;}
    if(it->is_symlink(ec)){it.disable_recursion_pending();continue;}
    if(!it->is_regular_file(ec)||lower(it->path().filename().string())!="ht2save.dat")continue;
    const auto size=it->file_size(ec);if(ec||size<1024||size>(16u<<20))continue;
    const bool title=lower(it->path().string()).find("5841096a")!=std::string::npos;
    const auto when=it->last_write_time(ec);
    if(best.empty()||(title&&!best_title)||(title==best_title&&when>best_time)){best=it->path();best_time=when;best_title=title;}
  }
  return best;
}
// Returns the Xenia save that was imported. The current save (if any) is exported to
// <root>/save_backups first; nothing is deleted.
inline fs::path ImportXenia(const fs::path& root,const fs::path& picked) {
  const auto user_data=UserDataRoot(root);
  if(Within(picked,user_data))throw std::runtime_error("Escolha a pasta do Xenia, nao a pasta de saves deste jogo.");
  const auto source=FindXeniaSave(picked);
  if(source.empty())throw std::runtime_error("Nenhum ht2save.dat encontrado. Escolha a pasta do Xenia (ou a pasta content dele).");
  // The profile already used here, or the default player profile.
  fs::path profile=user_data/"B13EBABEBABEBABE";
  if(fs::is_directory(user_data))for(const auto& p:fs::directory_iterator(user_data))
    if(p.is_directory()&&ProfileName(p.path().filename().string())&&fs::is_directory(p.path()/"5841096A"/"00000001")){profile=p.path();break;}
  const auto dest=profile/"5841096A"/"00000001"/"ht2save.dat"/"ht2save.dat";
  const auto header=profile/"5841096A"/"Headers"/"00000001"/"ht2save.dat.header";
  // Xenia's header sits beside its save: <title>/Headers/00000001/ht2save.dat.header.
  fs::path xenia_header;
  if(source.parent_path().filename()=="ht2save.dat")
    xenia_header=source.parent_path().parent_path().parent_path()/"Headers"/"00000001"/"ht2save.dat.header";
  if(!fs::is_regular_file(header)&&(xenia_header.empty()||!fs::is_regular_file(xenia_header)))
    throw std::runtime_error("Abra o jogo uma vez e deixe ele salvar; depois importe o save do Xenia.");
  bool has_current=false;
  if(fs::is_directory(profile/"5841096A"/"00000001"))
    for(const auto& f:fs::recursive_directory_iterator(profile/"5841096A"/"00000001"))if(f.is_regular_file()){has_current=true;break;}
  if(has_current)Export(root,root/"save_backups");
  fs::create_directories(dest.parent_path());
  fs::copy_file(source,dest,fs::copy_options::overwrite_existing);
  if(!fs::is_regular_file(header)){fs::create_directories(header.parent_path());fs::copy_file(xenia_header,header);}
  return source;
}
inline void Import(const fs::path& root,const fs::path& backup) {
  std::ifstream marker(backup/"Hydro-Thunder-backup.txt");std::string version;std::getline(marker,version);
  if(version!="HTH-save-backup-v1"||!fs::is_directory(backup/"user_data"))
    throw std::runtime_error("Selecione a pasta Hydro-Thunder-saves criada ao exportar os saves.");
  // Validate and stage everything before touching current progress.
  const auto stage=root/"save_backups"/("restore-stage-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if(!CopySaves(backup/"user_data",stage))throw std::runtime_error("O backup nao contem saves deste jogo.");
  bool has_current=false;
  const auto user_data=UserDataRoot(root);
  if(fs::is_directory(user_data))for(const auto& profile:fs::directory_iterator(user_data)) {
    const auto current=profile.path()/"5841096A"/"00000001";
    if(profile.is_directory()&&ProfileName(profile.path().filename().string())&&fs::is_directory(current))
      for(const auto& file:fs::recursive_directory_iterator(current))if(file.is_regular_file()){has_current=true;break;}
  }
  if(has_current)Export(root,root/"save_backups");
  CopySaves(stage,user_data);
}
}
