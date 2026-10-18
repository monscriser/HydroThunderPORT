#include "ht_launch.h"
#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
  wchar_t path[32768];auto size=GetModuleFileNameW(nullptr,path,32768);
  if(!size || size>=32768)return 1;
  try{
    auto root=std::filesystem::path(path).parent_path();
    if(std::filesystem::exists(root/"data/hydrothunder.exe"))root/="data";
#if defined(HT_DIRECT_ENTRY)
    ht::LaunchGame(root,false);
#else
    ht::LaunchGame(root,true);
#endif
    return 0;}
  catch(const std::exception&){MessageBoxW(nullptr,L"Nao foi possivel abrir o configurador. Mantenha este arquivo junto do hydrothunder.exe.",L"Hydro Thunder",MB_OK|MB_ICONERROR);return 1;}
}
#else
#include <unistd.h>
int main(){char path[8192];auto size=readlink("/proc/self/exe",path,sizeof(path));if(size<=0 || size==sizeof(path))return 1;
  try{ht::LaunchGame(std::filesystem::path(std::string(path,size)).parent_path(),true);return 0;}catch(...){return 1;}}
#endif
