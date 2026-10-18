#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#if defined(_WIN32)
#include <windows.h>
#include <tlhelp32.h>
#endif
namespace ht::steam {
namespace fs=std::filesystem;
inline std::string Utf8(const fs::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
struct Node{uint8_t type=0;std::string key,value;std::vector<Node> children;};
inline std::string CString(const std::string& bytes,size_t& pos){auto end=bytes.find('\0',pos);if(end==std::string::npos)throw std::runtime_error("Arquivo da Steam incompleto.");auto s=bytes.substr(pos,end-pos);pos=end+1;return s;}
inline std::vector<Node> ReadNodes(const std::string& bytes,size_t& pos,int depth=0){
  if(depth>32)throw std::runtime_error("Arquivo da Steam invalido.");std::vector<Node> nodes;
  while(pos<bytes.size()){
    const uint8_t type=uint8_t(bytes[pos++]);if(type==8)return nodes;
    Node n;n.type=type;n.key=CString(bytes,pos);
    if(type==0)n.children=ReadNodes(bytes,pos,depth+1);
    else if(type==1)n.value=CString(bytes,pos);
    else if(type==2 || type==3 || type==4 || type==6 || type==7){size_t count=type==7?8:4;if(bytes.size()-pos<count)throw std::runtime_error("Arquivo da Steam incompleto.");n.value=bytes.substr(pos,count);pos+=count;}
    else throw std::runtime_error("Formato de atalhos da Steam nao suportado. Nenhum arquivo foi alterado.");
    nodes.push_back(std::move(n));
  }throw std::runtime_error("Arquivo da Steam sem terminador.");
}
inline void WriteNodes(std::string& out,const std::vector<Node>& nodes){for(const auto& n:nodes){out+=char(n.type);out+=n.key;out+='\0';if(n.type==0)WriteNodes(out,n.children);else{out+=n.value;if(n.type==1)out+='\0';}}out+=char(8);}
inline Node Str(std::string key,std::string value){return {1,std::move(key),std::move(value),{}};}
inline Node Int(std::string key,uint32_t value){std::string b;for(int i=0;i<4;++i)b+=char(value>>(i*8));return {2,std::move(key),b,{}};}
inline bool SameKey(const std::string& a,const std::string& b){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(std::tolower(uint8_t(a[i]))!=std::tolower(uint8_t(b[i])))return false;return true;}
inline const Node* Find(const std::vector<Node>& nodes,const std::string& key){for(const auto& n:nodes)if(SameKey(n.key,key))return &n;return nullptr;}
inline uint32_t AppId(const std::string& quoted_exe,const std::string& name){uint32_t crc=0xFFFFFFFF;for(uint8_t c:quoted_exe+name){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^(0xEDB88320u & (0u-(crc&1)));}return (~crc)|0x80000000u;}
inline bool Running(){
#if defined(_WIN32)
  HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return true;
  PROCESSENTRY32W item{};item.dwSize=sizeof(item);bool running=false;
  if(Process32FirstW(snap,&item))do{if(_wcsicmp(item.szExeFile,L"steam.exe")==0){running=true;break;}}while(Process32NextW(snap,&item));CloseHandle(snap);return running;
#else
  std::error_code ec;for(const auto& p:fs::directory_iterator("/proc",ec)){std::ifstream in(p.path()/"comm");std::string name;std::getline(in,name);if(name=="steam")return true;}return false;
#endif
}
inline std::vector<fs::path> Profiles(){
  std::vector<fs::path> roots,result;
#if defined(_WIN32)
  wchar_t path[32768];DWORD count=sizeof(path);
  if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,path,&count)==ERROR_SUCCESS)roots.emplace_back(path);
  if(const char* dir=std::getenv("ProgramFiles(x86)"))roots.emplace_back(fs::path(dir)/"Steam");
#else
  if(const char* home=std::getenv("HOME")){roots.emplace_back(fs::path(home)/".steam/steam");roots.emplace_back(fs::path(home)/".local/share/Steam");roots.emplace_back(fs::path(home)/".var/app/com.valvesoftware.Steam/.local/share/Steam");}
#endif
  for(const auto& root:roots){std::error_code ec;for(const auto& p:fs::directory_iterator(root/"userdata",ec)){
    const auto name=p.path().filename().string();if(name.empty() || name=="0" || !std::all_of(name.begin(),name.end(),[](char c){return c>='0'&&c<='9';}))continue;
    if(!fs::is_directory(p.path()/"config"))continue;auto path=fs::weakly_canonical(p.path()/"config",ec);if(!ec && std::find(result.begin(),result.end(),path)==result.end())result.push_back(path);
  }}return result;
}
inline uint32_t Export(const fs::path& game_root,const fs::path& config,bool check_running=true){
  if(check_running && Running())throw std::runtime_error("Feche a Steam completamente e clique novamente. A Steam aberta pode sobrescrever os atalhos.");
  if(!fs::is_directory(config) || config.filename()!="config" || config.parent_path().parent_path().filename()!="userdata")throw std::runtime_error("Selecione a pasta config da sua conta dentro de Steam/userdata.");
  const auto target=config/"shortcuts.vdf";if(fs::is_symlink(fs::symlink_status(target)))throw std::runtime_error("Arquivo de atalhos simbolico nao suportado.");
  std::vector<Node> nodes;if(fs::exists(target)){if(fs::file_size(target)>16*1024*1024)throw std::runtime_error("Arquivo de atalhos muito grande.");std::ifstream in(target,std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(in)),{});size_t pos=0;nodes=ReadNodes(bytes,pos);if(pos!=bytes.size())throw std::runtime_error("Dados extras no arquivo da Steam; nada foi alterado.");}
  if(nodes.empty())nodes.push_back({0,"shortcuts",{}, {}});
  auto it=std::find_if(nodes.begin(),nodes.end(),[](const Node& n){return n.type==0 && n.key=="shortcuts";});if(it==nodes.end())throw std::runtime_error("Arquivo da Steam nao contem atalhos.");
  const std::string name="Hydro Thunder Hurricane";
#if defined(_WIN32)
  const auto exe=fs::is_regular_file(game_root.parent_path()/"HydroThunder.exe") ? game_root.parent_path()/"HydroThunder.exe" : game_root/"hydrothunder.exe";
#else
  auto exe=game_root/"hydrothunder";
  if(const auto* image=std::getenv("APPIMAGE");image && fs::is_regular_file(image))exe=fs::u8path(image);
#endif
  if(!fs::is_regular_file(exe))throw std::runtime_error("Executavel do jogo nao encontrado.");
  const auto quoted="\""+Utf8(fs::absolute(exe))+"\"";const auto id=AppId(quoted,name);
  const auto start_dir=fs::absolute(exe).parent_path();
  auto icon=config/"grid"/(std::to_string(id)+"_icon.ico");
  // The Steam art pack's multi-size icon wins over the small window icon.
  fs::path icon_src;
  for(const auto& candidate:{game_root/"steam_art/monscriser/icon.ico",game_root/"steam_art/icon.ico",game_root/"ui/icon.ico"})
    if(fs::is_regular_file(candidate)){icon_src=candidate;break;}
  if(!icon_src.empty()){
    fs::create_directories(icon.parent_path());fs::copy_file(icon_src,icon,fs::copy_options::overwrite_existing);
  }else icon=config/"grid"/(std::to_string(id)+"p.jpg");
  Node shortcut{0,"",{}, {Int("appid",id),Str("AppName",name),Str("Exe",quoted),Str("StartDir","\""+Utf8(start_dir)+"\""),Str("icon",Utf8(icon)),Str("ShortcutPath",""),Str("LaunchOptions","--ht_direct_launch=true"),Int("IsHidden",0),Int("AllowDesktopConfig",1),Int("AllowOverlay",1),Int("OpenVR",0),Int("Devkit",0),Str("DevkitGameID",""),Int("DevkitOverrideAppID",0),Int("LastPlayTime",0),Str("FlatpakAppID",""),Node{0,"tags",{}, {Str("0","Hydro Thunder")}}}};
  bool replaced=false;for(auto& n:it->children){auto old_name=Find(n.children,"AppName"),old_exe=Find(n.children,"Exe");if(old_name && old_exe && old_name->value==name && old_exe->value==quoted){
    for(const auto& update:shortcut.children){if(update.key!="appid" && update.key!="LaunchOptions" && update.key!="StartDir")continue;auto field=std::find_if(n.children.begin(),n.children.end(),[&](const Node& x){return SameKey(x.key,update.key);});if(field==n.children.end())n.children.push_back(update);else field->value=update.value;}
    replaced=true;break;}}
  if(!replaced){size_t index=0;while(Find(it->children,std::to_string(index)))++index;shortcut.key=std::to_string(index);it->children.push_back(std::move(shortcut));}
  std::string bytes;WriteNodes(bytes,nodes);
  const auto stamp=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
  const auto temp=config/("shortcuts.vdf.ht-new-"+stamp);
  {std::ofstream out(temp,std::ios::binary);out.write(bytes.data(),bytes.size());out.close();if(!out)throw std::runtime_error("Nao foi possivel salvar o atalho.");}
  if(fs::exists(target))fs::copy_file(target,config/("shortcuts.vdf.ht-backup-"+stamp));
#if defined(_WIN32)
  if(!MoveFileExW(temp.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Nao foi possivel atualizar os atalhos da Steam.");
#else
  fs::rename(temp,target);
#endif
  // Downloaded SteamGridDB art packs may be placed here. Prefer the requested
  // artist folder; fallback art of each missing type can live in the parent.
  const auto art=game_root/"steam_art";const auto grid=config/"grid";
  bool logo_copied=false;
  for(const auto& [kind,suffix]:std::vector<std::pair<std::string,std::string>>{{"grid","p"},{"banner",""},{"hero","_hero"},{"logo","_logo"},{"icon","_icon"}}){
    bool copied=false;for(const auto& artist:std::vector<fs::path>{art/"monscriser",art}){for(const auto& extension:{".png",".jpg"}){auto src=artist/(kind+extension);if(fs::is_regular_file(src)){fs::create_directories(grid);fs::copy_file(src,grid/(std::to_string(id)+suffix+extension),fs::copy_options::overwrite_existing);copied=true;break;}}if(copied)break;}
    if(kind=="logo")logo_copied=copied;
  }
  // Steam keeps the logo placement over the hero in <id>.json. The default
  // (bottom-left) covers the boat in the shipped hero, so pin it to the sky.
  if(logo_copied){
    std::ofstream position(grid/(std::to_string(id)+".json"),std::ios::binary|std::ios::trunc);
    position<<R"({"nVersion":1,"logoPosition":{"pinnedPosition":"UpperLeft","nWidthPct":36,"nHeightPct":47}})";
  }
  return id;
}
}
