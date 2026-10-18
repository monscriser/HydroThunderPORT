#pragma once
#include <rex/runtime.h>
#include <rex/filesystem.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/entry.h>
#include <rex/logging.h>
#include <rex/system/xam/content_manager.h>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <memory>
#include "ht_library_paths.h"
namespace ht {
inline bool ReplaceResourceText(std::string& xml,const std::string& key,const std::string& value) {
  auto data=xml.find("<data name=\""+key+"\"");if(data==std::string::npos)return false;
  auto end=xml.find("</data>",data),start=xml.find("<value>",data);
  if(end==std::string::npos||start==std::string::npos||start>end)return false;
  start+=7;auto finish=xml.find("</value>",start);
  if(finish==std::string::npos||finish>end)return false;
  xml.replace(start,finish-start,value);return true;
}
// pt-BR tables shipped beside the executable (translations/pt-br-*.txt): one
// "key<TAB>value" per line, value is raw resx XML with \\ \n \t escapes. The
// Portuguese menus of the original game are pt-PT; these replace them at launch
// in a generated copy, never in the game files.
inline std::map<std::string,std::string> LoadPtBrTable(const std::string& name){
  std::map<std::string,std::string> table;
  std::ifstream in(rex::filesystem::GetExecutableFolder()/"translations"/name,std::ios::binary);
  std::string line;
  while(std::getline(in,line)){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line.empty()||line[0]=='#')continue;
    const auto tab=line.find('\t');if(tab==std::string::npos)continue;
    std::string value;value.reserve(line.size()-tab);
    for(size_t i=tab+1;i<line.size();++i){
      if(line[i]=='\\'&&i+1<line.size()){
        const char c=line[++i];value+=c=='n'?'\n':c=='t'?'\t':c;
      }else value+=line[i];
    }
    table[line.substr(0,tab)]=std::move(value);
  }
  return table;
}
inline size_t ApplyTable(std::string& xml,const std::map<std::string,std::string>& table){
  size_t count=0;for(const auto& [key,value]:table)count+=ReplaceResourceText(xml,key,value);return count;
}
inline std::string ReadAll(const std::filesystem::path& path){
  std::ifstream input(path,std::ios::binary);if(!input)return {};
  return {(std::istreambuf_iterator<char>(input)),{}};
}
inline void WriteAll(const std::filesystem::path& path,const std::string& text){
  std::ofstream output(path,std::ios::binary);output<<text;output.close();
  if(!output)throw std::runtime_error("Nao foi possivel preparar os textos do jogo.");
}
// DLC string tables live in the content package, mounted by the title at run
// time. When one is mounted, mirror its Strings folder (pt-pt translated) and
// point "<content device>Strings" at the copy; the package stays untouched.
inline void InstallDlcTranslation(rex::Runtime* runtime,const std::filesystem::path& root){
  rex::system::xam::SetContentMountHook([runtime,root](const std::string& device_path,const std::filesystem::path& package){
    try{
      const auto strings=package/"Strings";
      if(!std::filesystem::is_regular_file(strings/"pt-pt.resx"))return;
      static const auto table=LoadPtBrTable("pt-br-dlc.txt");
      if(table.empty())return;
      const auto overlay=root/"content_ui"/("dlc-"+package.filename().string());
      std::filesystem::create_directories(overlay);
      size_t applied=0;
      for(const auto& entry:std::filesystem::directory_iterator(strings)){
        if(!entry.is_regular_file())continue;
        auto xml=ReadAll(entry.path());
        if(entry.path().filename()=="pt-pt.resx")applied=ApplyTable(xml,table);
        WriteAll(overlay/entry.path().filename(),xml);
      }
      auto* vfs=runtime->file_system();
      const std::string mount="\\Device\\HTHStrings\\"+package.filename().string();
      static std::set<std::string> registered;  // one overlay device per package, reused on remount
      if(!registered.count(mount)){
        auto device=std::make_unique<rex::filesystem::HostPathDevice>(mount,overlay,true);
        if(!device->Initialize()||!vfs->RegisterDevice(std::move(device)))return;
        registered.insert(mount);
      }
      // Root-name symlinks resolve "Pack01:\Strings" to "<device>\\Strings" (the
      // device path already ends in a backslash), so cover both spellings.
      vfs->RegisterSymbolicLink(device_path+"Strings",mount);
      vfs->RegisterSymbolicLink(device_path+"\\Strings",mount);
      REXLOG_WARN("[ht-ptbr] DLC {}: {} pt-BR texts active",package.filename().string(),applied);
    }catch(const std::exception& error){REXLOG_WARN("[ht-ptbr] DLC text preparation failed: {}",error.what());}
  });
}
inline bool InstallOwnedContentLabels(rex::Runtime* runtime,const std::filesystem::path& root,bool content_owned=true) {
  try {
    InstallDlcTranslation(runtime,root);
    struct Label {const char* locale;const char* text;};
    const Label labels[]={{"en-us","Purchased Content"},{"pt-pt","Conteúdo comprado"},
      {"fr-fr","Contenu acheté"},{"es-es","Contenido comprado"},{"it-it","Contenuti acquistati"},
      {"de-de","Gekaufte Inhalte"},{"ja-jp","購入済みコンテンツ"},{"ko-kr","구매한 콘텐츠"},
      {"zh-cht","已購買內容"}};
    const auto overlay=root/"content_ui";
    std::filesystem::create_directories(overlay);
    std::string game_device;
    auto* vfs=runtime->file_system();
    if(!vfs->FindSymbolicLink("game:",game_device))return false;
    const auto ptbr=LoadPtBrTable("pt-br-base.txt");
    size_t count=0,translated=0;
    for(const auto& label:labels){
      const auto name=std::string(label.locale)+".resx";
      auto xml=ReadAll(GameDirectory(root)/"Strings"/name);
      if(xml.empty())continue;
      bool changed=false;
      if(name=="pt-pt.resx"&&!ptbr.empty()){translated=ApplyTable(xml,ptbr);changed=translated>0;}
      if(content_owned&&ReplaceResourceText(xml,"MainMenu_014b",label.text))changed=true;
      // Every locale is mirrored: the overlay replaces the whole Strings folder.
      WriteAll(overlay/name,xml);
      count+=changed;
    }
    if(!count)return false;
    auto device=std::make_unique<rex::filesystem::HostPathDevice>("\\Device\\HTHOwnedContentUI",overlay,true);
    if(!device->Initialize()||!vfs->RegisterDevice(std::move(device)))return false;
    // Redirect the directory as well as ordinary file opens: the title may
    // enumerate translations or open them relative to a directory handle.
    vfs->RegisterSymbolicLink(game_device+"\\Strings","\\Device\\HTHOwnedContentUI");
    auto* directory=vfs->ResolvePath("game:\\Strings");
    if(!directory || directory->device()->mount_path()!="\\Device\\HTHOwnedContentUI")return false;
    if(translated)REXLOG_WARN("[ht-ptbr] {} pt-BR texts active",translated);
    if(content_owned)REXLOG_WARN("[ht-content] Installed-content captions active in {} languages",count);
    return true;
  }catch(const std::exception& error){REXLOG_WARN("[ht-content] Menu caption preparation failed: {}",error.what());return false;}
}
}
