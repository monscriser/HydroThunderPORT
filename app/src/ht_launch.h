#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <cstdlib>
#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
#endif
namespace ht {
inline void LaunchGame(const std::filesystem::path& root, bool launcher=false) {
#if defined(_WIN32)
  const auto exe=root/L"hydrothunder.exe";
  std::wstring command=L"\""+exe.wstring()+L"\" "+(launcher ? L"--ht_import_on_start=true" : L"--ht_direct_launch=true");
  STARTUPINFOW start{};start.cb=sizeof(start);start.dwFlags=STARTF_USESHOWWINDOW;
  start.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION process{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&start,&process))
    throw std::runtime_error("Nao foi possivel abrir o jogo.");
  if(std::getenv("HT_LAUNCH_TRACE")){
    std::ofstream log(root/"launcher-test.log",std::ios::app);log<<"child="<<process.dwProcessId<<" launcher="<<launcher<<"\n";
    WaitForSingleObject(process.hProcess,5000);DWORD code;GetExitCodeProcess(process.hProcess,&code);log<<"exit="<<code<<"\n";
  }
  CloseHandle(process.hThread);CloseHandle(process.hProcess);
#else
  auto exe=root/"hydrothunder";
  if(const char* image=std::getenv("APPIMAGE");image && std::filesystem::is_regular_file(image))
    exe=std::filesystem::u8path(image);
  std::string name=exe.string(),option=launcher ? "--ht_import_on_start=true" : "--ht_direct_launch=true";
  char* args[]={name.data(),option.data(),nullptr};pid_t pid;
  posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
  const auto directory=root.string();posix_spawn_file_actions_addchdir_np(&actions,directory.c_str());
  const auto error=posix_spawn(&pid,exe.c_str(),&actions,nullptr,args,environ);
  posix_spawn_file_actions_destroy(&actions);
  if(error)throw std::runtime_error("Nao foi possivel abrir o jogo.");
#endif
}
}
