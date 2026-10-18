#pragma once
// Optional integration with the user's Steam purchase. No LS binaries are shipped.
#include "ht_library_paths.h"
#include <algorithm>
#include <vector>
#include <sstream>
#include <chrono>
#include <cctype>
#if defined(_WIN32)
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif
namespace ht::lossless {
namespace fs=std::filesystem;
inline constexpr const char* StoreUrl="https://store.steampowered.com/app/993090/Lossless_Scaling/";
inline constexpr const char* InstallUrl="steam://install/993090";
inline constexpr const char* LinuxHelp="https://lsfg-vk.dev/docs/installation/";
inline std::string Utf8(const fs::path& p){auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
inline bool File(const fs::path& p){std::error_code ec;return fs::is_regular_file(p,ec);}
inline std::string Read(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
// Valve text VDF tokenizer: quoted paths, escaped backslashes, comments and nesting.
inline std::vector<std::string> Tokens(const std::string& s){
  std::vector<std::string> out;
  for(size_t i=0;i<s.size();){
    if(std::isspace(static_cast<unsigned char>(s[i]))){++i;continue;}
    if(s[i]=='/'&&i+1<s.size()&&s[i+1]=='/'){i=s.find('\n',i);if(i==std::string::npos)break;continue;}
    if(s[i]=='{'||s[i]=='}'){out.emplace_back(1,s[i++]);continue;}
    std::string token;
    if(s[i]=='"'){++i;while(i<s.size()&&s[i]!='"'){
      if(s[i]=='\\'&&i+1<s.size()&&(s[i+1]=='\\'||s[i+1]=='"'))++i;
      token+=s[i++];
    }if(i<s.size())++i;}
    else {while(i<s.size()&&!std::isspace(static_cast<unsigned char>(s[i]))&&s[i]!='{'&&s[i]!='}')token+=s[i++];}
    out.push_back(std::move(token));
  }return out;
}
inline std::string Value(const std::string& text,const std::string& key){auto t=Tokens(text);for(size_t i=0;i+1<t.size();++i)if(t[i]==key&&t[i+1]!="{")return t[i+1];return {};}
inline void Add(std::vector<fs::path>& out,const fs::path& path){
  if(path.empty())return;std::error_code ec;auto p=fs::weakly_canonical(path,ec);if(ec)p=path.lexically_normal();
  if(std::find(out.begin(),out.end(),p)==out.end())out.push_back(p);
}
inline std::vector<fs::path> SteamRoots(){
  std::vector<fs::path> roots;
#if defined(_WIN32)
  wchar_t p[32768];DWORD n=sizeof(p);
  if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,p,&n)==ERROR_SUCCESS)Add(roots,p);
  for(auto key:{L"Software\\Valve\\Steam",L"Software\\WOW6432Node\\Valve\\Steam"}){n=sizeof(p);if(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"InstallPath",RRF_RT_REG_SZ,nullptr,p,&n)==ERROR_SUCCESS)Add(roots,p);}
  for(auto key:{"ProgramFiles(x86)","ProgramFiles"})if(auto v=std::getenv(key))Add(roots,fs::u8path(v)/"Steam");
#else
  if(auto h=std::getenv("HOME"))for(auto rel:{".steam/steam",".steam/root",".local/share/Steam",".var/app/com.valvesoftware.Steam/.local/share/Steam"})Add(roots,fs::u8path(h)/rel);
  if(auto d=std::getenv("XDG_DATA_HOME"))Add(roots,fs::u8path(d)/"Steam");
#endif
  if(auto p=std::getenv("STEAM_COMPAT_CLIENT_INSTALL_PATH"))Add(roots,fs::u8path(p));
  return roots;
}
inline std::vector<fs::path> Libraries(const std::vector<fs::path>& roots){
  auto result=roots;
  for(const auto& root:roots){auto t=Tokens(Read(root/"steamapps/libraryfolders.vdf"));
    for(size_t i=0;i+1<t.size();++i){
      const auto& k=t[i];const auto& v=t[i+1];
      if(k=="path" || (!k.empty()&&std::all_of(k.begin(),k.end(),[](char c){return c>='0'&&c<='9';})&&v!="{"&&fs::u8path(v).is_absolute()))Add(result,fs::u8path(v));
    }
  }return result;
}
struct Options{bool enabled=false;int multiplier=2;bool above_refresh=false;bool performance=false;};
struct SessionStatus{bool requested=false;int multiplier=2;bool above_refresh=false;};
inline SessionStatus session_status;
inline Options Load(){Options o;std::ifstream in(SettingsDirectory()/"lossless-scaling.txt");std::string version;int enabled=0;in>>version>>enabled>>o.multiplier;if(version!="HT-LS-1"&&version!="HT-LS-2")return {};o.enabled=enabled==1;o.multiplier=std::clamp(o.multiplier,2,4);if(version=="HT-LS-2")in>>o.above_refresh>>o.performance;return o;}
inline void Save(const Options& o){auto dir=SettingsDirectory();fs::create_directories(dir);auto temp=dir/"lossless-scaling.new",dest=dir/"lossless-scaling.txt";
  {std::ofstream out(temp);out<<"HT-LS-2\n"<<o.enabled<<"\n"<<std::clamp(o.multiplier,2,4)<<"\n"<<o.above_refresh<<"\n"<<o.performance<<"\n";out.close();if(!out)throw std::runtime_error("Could not save Lossless Scaling settings.");}
#if defined(_WIN32)
  if(!MoveFileExW(temp.c_str(),dest.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Could not save Lossless Scaling settings.");
#else
  fs::rename(temp,dest);
#endif
}
struct Detection{fs::path folder,exe,dll,layer;bool purchased_files=false;bool Ready()const{
#if defined(_WIN32)
  return !exe.empty();
#else
  return !dll.empty()&&!layer.empty();
#endif
}};
inline Detection Detect(const std::vector<fs::path>& roots=SteamRoots()){
  Detection d;
  for(const auto& lib:Libraries(roots)){
    const auto manifest=Read(lib/"steamapps/appmanifest_993090.acf");
    auto install=Value(manifest,"installdir");
    std::vector<fs::path> candidates;
    if(!install.empty()){
      auto rel=fs::u8path(install);
      if(!rel.is_absolute()&&rel.parent_path().empty())candidates.push_back(lib/"steamapps/common"/rel);
    }
    candidates.push_back(lib/"steamapps/common/Lossless Scaling");
    for(const auto& folder:candidates){
      bool exe=File(folder/"LosslessScaling.exe"),dll=File(folder/"lsfg-vk.dll");
      if(!exe&&!dll)continue;
      d.purchased_files=true;
      if(d.folder.empty())d.folder=folder;
      if(exe&&d.exe.empty())d.exe=folder/"LosslessScaling.exe";
      if(dll&&d.dll.empty())d.dll=folder/"lsfg-vk.dll";
    }
  }
#if !defined(_WIN32)
  std::vector<fs::path> data;
  if(auto h=std::getenv("HOME"))Add(data,fs::u8path(h)/".local/share");
  if(auto h=std::getenv("XDG_DATA_HOME"))Add(data,fs::u8path(h));
  if(auto h=std::getenv("XDG_CONFIG_HOME"))Add(data,fs::u8path(h));
  else if(auto h=std::getenv("HOME"))Add(data,fs::u8path(h)/".config");
  Add(data,"/etc");Add(data,"/etc/xdg");
  const char* dirs=std::getenv("XDG_DATA_DIRS");std::istringstream stream(dirs?dirs:"/usr/local/share:/usr/share");std::string s;
  while(std::getline(stream,s,':'))if(!s.empty())Add(data,fs::u8path(s));
  for(const auto& base:data){auto p=base/"vulkan/implicit_layer.d/VkLayer_LSFGVK_frame_generation.json";
    if(File(p)&&Read(p).find("VK_LAYER_LSFGVK_frame_generation")!=std::string::npos){d.layer=p;break;}}
#endif
  return d;
}
#if defined(_WIN32)
inline std::vector<DWORD> LosslessProcesses(){
  std::vector<DWORD> ids;HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snap==INVALID_HANDLE_VALUE)return ids;
  PROCESSENTRY32W item{};item.dwSize=sizeof(item);
  if(Process32FirstW(snap,&item))do{if(_wcsicmp(item.szExeFile,L"LosslessScaling.exe")==0)ids.push_back(item.th32ProcessID);}while(Process32NextW(snap,&item));
  CloseHandle(snap);return ids;
}
inline bool LosslessRunning(){return !LosslessProcesses().empty();}
// LS is really scaling only while its untitled output window is shown over the
// game. Measured in races: started too early LS ran with no such window and
// generated nothing; whenever its counter showed frames, the window existed.
// The settings window (titled) and the tray icon do not count.
inline bool LosslessActive(){
  struct Search{std::vector<DWORD> ids;bool found=false;} search{LosslessProcesses()};
  if(search.ids.empty())return false;
  EnumWindows([](HWND hwnd,LPARAM param)->BOOL{
    auto& s=*reinterpret_cast<Search*>(param);DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
    if(std::find(s.ids.begin(),s.ids.end(),pid)==s.ids.end()||!IsWindowVisible(hwnd)||IsIconic(hwnd)||GetWindowTextLengthW(hwnd)>0)return TRUE;
    RECT r{};if(GetWindowRect(hwnd,&r)&&r.right-r.left>=320&&r.bottom-r.top>=240){s.found=true;return FALSE;}
    return TRUE;},reinterpret_cast<LPARAM>(&search));
  return search.found;
}
inline void Run(const fs::path& exe,std::wstring args,bool wait=false){
  std::wstring command=L"\""+exe.wstring()+L"\" "+args;STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,wait?CREATE_NO_WINDOW:0,nullptr,exe.parent_path().c_str(),&si,&pi))throw std::runtime_error("Could not start Lossless Scaling setup.");
  CloseHandle(pi.hThread);
  if(wait){auto result=WaitForSingleObject(pi.hProcess,15000);DWORD code=1;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);
    if(result!=WAIT_OBJECT_0)throw std::runtime_error("Lossless Scaling setup is still running; check lossless-profile.log before retrying.");
    if(code)throw std::runtime_error("Close Lossless Scaling and retry. See lossless-profile.log in the settings folder.");
  }else CloseHandle(pi.hProcess);
}
// LS only applies AutoScale when the game window becomes foreground after LS
// has finished loading. Measured: started together with the game, LS stayed
// idle; already loaded in the tray, it generated frames. So wait for it first.
inline void StartAndWaitReady(const fs::path& exe,const std::wstring& args){
  std::wstring command=L"\""+exe.wstring()+L"\" "+args;STARTUPINFOW si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
  if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&si,&pi))throw std::runtime_error("Could not start Lossless Scaling.");
  CloseHandle(pi.hThread);
  // LS started by the game closes with the game (also on a crash): the job
  // handle stays open for the game's lifetime and kills LS when it closes.
  if(HANDLE job=CreateJobObjectW(nullptr,nullptr)){
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))||!AssignProcessToJobObject(job,pi.hProcess))CloseHandle(job);
  }
  WaitForInputIdle(pi.hProcess,15000);
  WaitForSingleObject(pi.hProcess,3000);  // returns early only if LS exits
  CloseHandle(pi.hProcess);
}
// .NET XML preserves unknown settings and other game profiles; only our title is edited.
inline constexpr const char* ProfileScript=R"HTLS(param([int]$Enabled,[ValidateRange(2,4)][int]$Multiplier,[string]$Log,[int]$AboveRefresh=0,[int]$ShowCounter=0)
$ErrorActionPreference='Stop'
try {
  $cfg=Join-Path $env:LOCALAPPDATA 'Lossless Scaling\Settings.xml'
  $doc=New-Object System.Xml.XmlDocument
  $doc.XmlResolver=$null
  $settingsCreated=$false
  if(!(Test-Path -LiteralPath $cfg)){
    if(!$Enabled){exit 0}
    # Lossless Scaling 3.x shape from publicly shared Settings.xml examples.
    # Seed only a missing file; an existing installation is always preserved.
    $seed=@'
<?xml version="1.0" encoding="utf-8"?>
<Settings xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Hotkey>S</Hotkey><HotkeyModifierKeys>Alt Control</HotkeyModifierKeys>
  <GpuPreference>0</GpuPreference><GpuPreferenceChangeCount>0</GpuPreferenceChangeCount>
  <SetupPhase>5</SetupPhase><StartAsAdmin>false</StartAsAdmin>
  <StartAtWindowsStartup>false</StartAtWindowsStartup><MinimizeToTray>true</MinimizeToTray>
  <Language>System</Language><Theme>Dark</Theme>
  <FrameGenerationCollapsed>false</FrameGenerationCollapsed><CursorOptionsCollapsed>false</CursorOptionsCollapsed>
  <RenderingOptionsCollapsed>false</RenderingOptionsCollapsed><CaptureOptionsCollapsed>false</CaptureOptionsCollapsed>
  <GpuDisplayOptionsCollapsed>false</GpuDisplayOptionsCollapsed><CropInputOptionsCollapsed>false</CropInputOptionsCollapsed>
  <BehaviorCollapsed>false</BehaviorCollapsed><LegacyOptionsCollapsed>false</LegacyOptionsCollapsed>
  <GameProfiles><Profile>
    <Title>Default</Title><AutoScale>false</AutoScale><AutoScaleDelay>0</AutoScaleDelay>
    <ScalingMode>Auto</ScalingMode><ScalingFitMode>AspectRatio</ScalingFitMode><ScaleFactor>1.5</ScaleFactor>
    <ResizeBeforeScaling>false</ResizeBeforeScaling><WindowedMode>false</WindowedMode><ScalingType>Off</ScalingType>
    <FSRType>ORIGINAL</FSRType><LS1Type>BALANCED</LS1Type><LSFGType>BALANCED</LSFGType>
    <LSFGMode>X2</LSFGMode><LSFG2Mode>X2</LSFG2Mode><LSFG3Mode>X2</LSFG3Mode>
    <LSFG3Mode1>FIXED</LSFG3Mode1><LSFG3Multiplier>2</LSFG3Multiplier><LSFGResolutionScale>100</LSFGResolutionScale>
    <Anime4kType>S</Anime4kType><Sharpness>5</Sharpness><LS1Sharpness>1</LS1Sharpness><VRS>false</VRS>
    <FrameGeneration>Off</FrameGeneration><ClipCursor>false</ClipCursor><AdjustCursorSpeed>false</AdjustCursorSpeed>
    <HideCursor>false</HideCursor><ScaleCursor>false</ScaleCursor><SyncMode>DEFAULT</SyncMode>
    <MaxFrameLatency>1</MaxFrameLatency><DoubleBuffering>false</DoubleBuffering><GsyncSupport>false</GsyncSupport>
    <HdrSupport>true</HdrSupport><DrawFps>false</DrawFps><CaptureApi>WGC</CaptureApi>
    <PreferredGpuId>0</PreferredGpuId><OutputDisplayId>0</OutputDisplayId>
    <CropInputLeft>0</CropInputLeft><CropInputTop>0</CropInputTop><CropInputRight>0</CropInputRight>
    <CropInputBottom>0</CropInputBottom><MultiDisplayMode>false</MultiDisplayMode>
  </Profile></GameProfiles>
</Settings>
'@
    $doc.LoadXml($seed)
    $settingsCreated=$true
  }else{
    $readerSettings=New-Object System.Xml.XmlReaderSettings
    $readerSettings.DtdProcessing=[System.Xml.DtdProcessing]::Prohibit
    $reader=[System.Xml.XmlReader]::Create($cfg,$readerSettings)
    try{$doc.Load($reader)}finally{$reader.Dispose()}
  }
  $profiles=$doc.SelectSingleNode('/Settings/GameProfiles')
  if(!$profiles){throw 'Unsupported Lossless Scaling settings format.'}
  $title='Hydro Thunder Hurricane: Recompiled'
  $matches=@($profiles.SelectNodes('Profile') | Where-Object { $_.Title -eq $title })
  if($matches.Count -gt 1){throw 'Duplicate Hydro Thunder profiles; fix these in Lossless Scaling.'}
  $node=if($matches.Count){$matches[0]}else{$null}
  if(!$Enabled -and !$node){exit 0}
  $values=@{AutoScale=([bool]$Enabled).ToString().ToLowerInvariant()}
  if($Enabled){
    # AutoScale only matches an application when the profile has a filter.
    # Match the packaged game executable by basename so profiles keep working
    # when the user moves the launcher/game folder or updates the build.
    # LS 3.2.2 serializes this filter as <Path>.
    $values.Path='hydrothunder.exe'
    $values.ScalingType='Off';$values.FrameGeneration='LSFG3';$values.LSFG3Mode1='FIXED';$values.LSFG3Multiplier="$Multiplier";$values.DrawFps=([bool]$ShowCounter).ToString().ToLowerInvariant()
  }
  $syncBackup=Join-Path (Split-Path -Parent $Log) 'lossless-original-sync.xml'
  if($Enabled -and $AboveRefresh){
    $source=if($node){$node}else{$profiles.SelectSingleNode('Profile')}
    if(!$source -or !$source.SelectSingleNode('SyncMode') -or !$source.SelectSingleNode('MaxFrameLatency')){throw 'This LS version does not expose supported synchronization fields. Disable above-refresh mode.'}
    if(!(Test-Path -LiteralPath $syncBackup)){
      $saved=New-Object System.Xml.XmlDocument;$root=$saved.CreateElement('Original');[void]$saved.AppendChild($root)
      foreach($key in @('SyncMode','MaxFrameLatency')){$child=$saved.CreateElement($key);$child.InnerText=$source.SelectSingleNode($key).InnerText;[void]$root.AppendChild($child)}
      $saved.Save($syncBackup)
    }
    # DEFAULT disables both LS Vsync and explicit allow-tearing. DWM/driver can still cap output.
    $values.SyncMode='DEFAULT';$values.MaxFrameLatency='3'
  }elseif(Test-Path -LiteralPath $syncBackup){
    $saved=New-Object System.Xml.XmlDocument;$saved.XmlResolver=$null;$saved.Load($syncBackup)
    foreach($key in @('SyncMode','MaxFrameLatency')){$values[$key]=$saved.DocumentElement.SelectSingleNode($key).InnerText}
  }
  # The game starts LS with -StartMinimized; only with tray minimizing does LS
  # stay fully in the background instead of sitting on the taskbar.
  $tray=$doc.SelectSingleNode('/Settings/MinimizeToTray')
  $changed=!$node -or ($Enabled -and $tray -and $tray.InnerText -ne 'true')
  if($node){foreach($key in $values.Keys){if(!$node.SelectSingleNode($key) -or $node.SelectSingleNode($key).InnerText -ne $values[$key]){$changed=$true}}}
  if(!$changed){exit 0}
  # LS keeps running in the tray after a session and would keep (and later save)
  # its in-memory profile. Close it before writing; Prepare starts it again.
  $running=@(Get-Process -Name LosslessScaling -ErrorAction SilentlyContinue)
  if($running.Count){
    $running | Stop-Process -ErrorAction SilentlyContinue
    for($i=0;$i -lt 50 -and @(Get-Process -Name LosslessScaling -ErrorAction SilentlyContinue).Count;$i++){Start-Sleep -Milliseconds 200}
    if(@(Get-Process -Name LosslessScaling -ErrorAction SilentlyContinue).Count){throw 'Close Lossless Scaling before changing its Hydro Thunder profile.'}
    # Re-read: LS may have saved its settings while closing.
    if(!$settingsCreated){
      $doc=New-Object System.Xml.XmlDocument;$doc.XmlResolver=$null
      $reader=[System.Xml.XmlReader]::Create($cfg,$readerSettings);try{$doc.Load($reader)}finally{$reader.Dispose()}
      $profiles=$doc.SelectSingleNode('/Settings/GameProfiles');if(!$profiles){throw 'Unsupported Lossless Scaling settings format.'}
      $node=@($profiles.SelectNodes('Profile') | Where-Object { $_.Title -eq $title })[0]
    }
  }
  if(!$node){
    $template=@($profiles.SelectNodes('Profile') | Where-Object {$_.Title -eq 'Default'})[0]
    if(!$template){$template=$profiles.SelectSingleNode('Profile')}
    if(!$template){throw 'No default profile exists in Lossless Scaling settings.'}
    foreach($key in @('AutoScale','FrameGeneration','LSFG3Mode1','LSFG3Multiplier')){if(!$template.SelectSingleNode($key)){throw 'Unsupported LSFG3 profile version; configure Lossless Scaling manually.'}}
    $node=$template.CloneNode($true);$node.Title=$title;[void]$profiles.AppendChild($node)
  }
  if($Enabled){$tray=$doc.SelectSingleNode('/Settings/MinimizeToTray');if($tray){$tray.InnerText='true'}}
  # LS 3.2.2 writes <Path> right after <Title>; keep that order for new filters.
  foreach($key in $values.Keys){$child=$node.SelectSingleNode($key);if(!$child){$child=$doc.CreateElement($key);$anchor=$node.SelectSingleNode('Title');if($key -eq 'Path' -and $anchor){[void]$node.InsertAfter($child,$anchor)}else{[void]$node.AppendChild($child)}};$child.InnerText=$values[$key]}
  if($settingsCreated){[System.IO.Directory]::CreateDirectory((Split-Path -Parent $cfg)) | Out-Null}
  $temp="$cfg.hydrothunder.new";$doc.Save($temp)
  if($settingsCreated){
    [System.IO.File]::Move($temp,$cfg)
  }else{
    $backup="$cfg.hydrothunder-$(Get-Date -Format 'yyyyMMdd-HHmmss-ffff').bak"
    [System.IO.File]::Replace($temp,$cfg,$backup)
  }
  ('Hydro Thunder profile configured. Settings.xml bootstrapped automatically: '+$settingsCreated) | Set-Content -LiteralPath $Log
} catch { $_.Exception.Message | Set-Content -LiteralPath $Log;exit 1 }
)HTLS";
// Integration tests only: shows LS's own counter as proof of generated frames.
inline bool TestCounter(){auto t=std::getenv("HT_BOOT_TEST");auto c=std::getenv("HT_TEST_LS_COUNTER");return t&&c&&std::string(t)=="1"&&std::string(c)=="1";}
inline void ConfigureWindows(const Options& o){
  if(!o.enabled){auto local=std::getenv("LOCALAPPDATA");if(!local||Read(fs::u8path(local)/"Lossless Scaling/Settings.xml").find("Hydro Thunder Hurricane: Recompiled")==std::string::npos)return;}
  auto dir=SettingsDirectory();fs::create_directories(dir);auto script=dir/"lossless-profile.ps1";
  {std::ofstream out(script,std::ios::binary);out<<ProfileScript;if(!out)throw std::runtime_error("Could not write Lossless Scaling helper.");}
  wchar_t system[32768];if(!GetSystemDirectoryW(system,32768))throw std::runtime_error("Windows PowerShell not found.");
  Run(fs::path(system)/"WindowsPowerShell/v1.0/powershell.exe",L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \""+script.wstring()+L"\" -Enabled "+(o.enabled?L"1":L"0")+L" -Multiplier "+std::to_wstring(o.multiplier)+L" -AboveRefresh "+(o.above_refresh?L"1":L"0")+L" -ShowCounter "+(TestCounter()?L"1":L"0")+L" -Log \""+(dir/"lossless-profile.log").wstring()+L"\"",true);
}
#endif
// Called after config loading but before the first Vulkan instance, only for play.
inline bool Prepare(bool playing){
  auto o=Load();
  session_status={false,o.multiplier,o.above_refresh};
#if defined(_WIN32)
  if(!playing)return false;
  if(o.enabled){auto d=Detect();if(!d.Ready())throw std::runtime_error("Lossless Scaling was not found. Open Launcher-Config to install it or turn frame generation off.");ConfigureWindows(o);
    // Background only: tray, no window. A second launch would bring an
    // already running LS window to the front, so reuse it instead.
    if(!LosslessRunning())StartAndWaitReady(d.exe,L"-StartMinimized");
    session_status.requested=true;return true;}
  // Disable only our AutoScale profile, so an earlier enabled session doesn't stick.
  ConfigureWindows(o);
#else
  if(!playing||!o.enabled){setenv("DISABLE_LSFGVK","1",1);return false;}
  auto d=Detect();if(!d.Ready())throw std::runtime_error("Install lsfg-vk and the Steam Lossless Scaling lsfg-vk branch, or disable frame generation in the launcher.");
  unsetenv("DISABLE_LSFGVK");setenv("LSFGVK_ENV","1",1);setenv("LSFGVK_DLL_PATH",d.dll.c_str(),1);
  setenv("LSFGVK_MULTIPLIER",std::to_string(o.multiplier).c_str(),1);
  setenv("LSFGVK_FLOW_SCALE","1.0",1);setenv("LSFGVK_PERFORMANCE_MODE",o.performance?"1":"0",1);
  setenv("LSFGVK_PACING_MODE","vsync",1);
  setenv("LSFGVK_OVERRIDE_PRESENT_MODE",o.above_refresh?"0":"1",1);
  fs::create_directories(SettingsDirectory()/"logs");setenv("LSFGVK_LOG_FILE",(SettingsDirectory()/"logs/lsfg-vk.log").c_str(),1);
#endif
  session_status.requested=o.enabled;
  return o.enabled;
}
}
