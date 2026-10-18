#include "ht_lossless.h"
#include "ht_video_policy.h"
#include "ht_config_edit.h"
#include <iostream>
namespace ls=ht::lossless;
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Write(const std::filesystem::path& p,const std::string& s){std::filesystem::create_directories(p.parent_path());std::ofstream(p)<<s;}
std::string Quote(const std::filesystem::path& p){std::string out="\"";for(char c:ls::Utf8(p)){if(c=='\\'||c=='"')out+='\\';out+=c;}return out+'"';}
int main(int argc,char** argv){try{
  if(argc>1&&std::string(argv[1])=="--report"){
    auto roots=ls::SteamRoots();for(const auto& p:ls::Libraries(roots))std::cout<<"library="<<ls::Utf8(p)<<'\n';
    auto d=ls::Detect(roots);std::cout<<"purchased_files="<<d.purchased_files<<" ready="<<d.Ready()<<"\nfolder="<<ls::Utf8(d.folder)<<"\nexe="<<ls::Utf8(d.exe)<<"\ndll="<<ls::Utf8(d.dll)<<"\nlayer="<<ls::Utf8(d.layer)<<'\n';return 0;
  }
#if defined(_WIN32)
  if(argc>2&&std::string(argv[1])=="--script"){std::ofstream(argv[2])<<ls::ProfileScript;return 0;}
#endif
  Check(argc>1,"Pass an isolated fixture directory");auto root=std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  Check(ht::video::SwapLimit(ht::video::RateCount-1)==0&&ht::video::ClockRate(ht::video::RateCount-1)==60,"Unlimited removes swap cap without 1000 Hz virtual clock");
  for(auto rate:ht::video::Rates)Check(rate!=50,"50 FPS was an example, not a launcher preset");
  const auto config=root/"config.toml";
  Write(config,"# user comment\nfps_limit = 60\nunknown = \"kept\"\n[Other]\nfps_limit = 12\n");
  {ht::ConfigEdit edit(config);edit.Set("fps_limit","0");edit.Set("vsync","true");edit.Commit();}
  auto edited=ls::Read(config);edited.erase(std::remove(edited.begin(),edited.end(),'\r'),edited.end());Check(edited.find("fps_limit = 0")!=std::string::npos&&edited.find("unknown = \"kept\"")!=std::string::npos&&edited.find("[Other]\nfps_limit = 12")!=std::string::npos,"Atomic config preserves comments, unknown values and sections");
  auto stamp=std::filesystem::last_write_time(config);{ht::ConfigEdit edit(config);edit.Set("fps_limit","0");edit.Set("vsync","true");edit.Commit();}
  Check(stamp==std::filesystem::last_write_time(config),"Unchanged config is not rewritten");
  auto primary=root/"Steam",secondary=root/"Other library with spaces",old=root/"Legacy library";
  Write(primary/"steamapps/libraryfolders.vdf","// comment\n\"libraryfolders\" {\n\"0\"{\"path\" "+Quote(primary)+"}\n\"1\"{\"path\" "+Quote(secondary)+" \"apps\"{\"993090\"\"123\"}}\"2\" "+Quote(old)+"}");
  auto libs=ls::Libraries({primary});Check(libs.size()==3,"Must find modern and legacy libraries without interpreting app IDs as paths");
  auto d=ls::Detect({primary});Check(!d.purchased_files,"Empty library must not appear installed");
  Write(secondary/"steamapps/appmanifest_993090.acf","\"AppState\"{\"appid\"\"993090\" \"installdir\"\"Custom LS install\"}");
  auto custom=secondary/"steamapps/common/Custom LS install";
  Write(custom/"LosslessScaling.exe","fixture");d=ls::Detect({primary});Check(d.purchased_files&&!d.exe.empty()&&d.dll.empty(),"Manifest install directory must be honored; Windows install is not Linux DLL");
  Write(custom/"lsfg-vk.dll","fixture");d=ls::Detect({primary});Check(!d.dll.empty(),"lsfg-vk branch DLL must be found");
  Write(primary/"steamapps/common/Lossless Scaling/LosslessScaling.exe","fixture");
  Check(ls::Detect({primary}).purchased_files,"Default folder fallback must work");
  Write(primary/"steamapps/appmanifest_993090.acf","\"AppState\"{\"installdir\"\"../../unrelated\"}");
  Check(ls::Detect({primary}).folder.filename()!="unrelated","Do not follow manifest path traversal");
  Check(ls::Tokens("\"path\"\"C:\\\\Games\\\\Steam\" //hi").at(1)=="C:\\Games\\Steam","VDF escaped backslashes");
#if !defined(_WIN32)
  setenv("HOME",(root/"home").c_str(),1);setenv("XDG_DATA_HOME",(root/"xdg").c_str(),1);
  setenv("HT_SETTINGS_ROOT",(root/"settings").c_str(),1);setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH",primary.c_str(),1);
  Write(root/"xdg/vulkan/implicit_layer.d/VkLayer_LSFGVK_frame_generation.json","{\"layer\":{\"name\":\"VK_LAYER_LSFGVK_frame_generation\"}}");
  ls::Save({true,3,true,false});Check(ls::Load().enabled&&ls::Load().multiplier==3&&ls::Load().above_refresh&&!ls::Load().performance,"Persistent options v2");
  Check(!ls::Prepare(false)&&std::string(std::getenv("DISABLE_LSFGVK"))=="1","Configurator excludes frame generation");
  Check(ls::Prepare(true),"Linux play requests frame generation with dependencies found");
  Check(!std::getenv("DISABLE_LSFGVK")&&std::string(std::getenv("LSFGVK_MULTIPLIER"))=="3","Per-process environment multiplier");
  Check(std::string(std::getenv("LSFGVK_OVERRIDE_PRESENT_MODE"))=="0"&&std::string(std::getenv("LSFGVK_PERFORMANCE_MODE"))=="0","Experimental above-refresh mode without lowering model quality");
  Check(std::string(std::getenv("LSFGVK_DLL_PATH"))==ls::Utf8(custom/"lsfg-vk.dll"),"Purchased DLL path passed without copying");
  ls::Save({false,2});Check(!ls::Prepare(true)&&std::getenv("DISABLE_LSFGVK"),"Turning off disables LSFG for game process");
  std::cout<<"PASS: isolated Linux environment activation, launcher exclusion, persistence and deactivation (no Vulkan frame generation exercised)\n";
#endif
  std::cout<<"PASS: multi-library discovery, custom install, missing dependency, legacy VDF, comments, escaped paths, traversal guard\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
