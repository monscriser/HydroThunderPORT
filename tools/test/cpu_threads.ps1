# CPU load of the game per thread (Windows): opens the isolated test copy like gpu_menu.ps1
# (menu, 60 FPS by default), waits, then measures each thread's CPU time over a window and
# prints the top threads (ms of CPU per second of wall time = per-mille of one core / 10).
# Usage: cpu_threads.ps1 [-Set 'k=v;...'] [-Cvars 'k=v;...'] [-Warmup 35] [-Window 15]
param([string]$Set='resolution_scale=2;ht_render_quality=0',[string]$Cvars='',[int]$Warmup=35,[int]$Window=15)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
$settings=Join-Path $workspace 'outputs/_teste_gpu/settings'
New-Item -ItemType Directory -Force $settings | Out-Null
$values=[ordered]@{fps_limit='60';ht_import_on_start='false';ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';
  window_width='1920';window_height='1080';ht_output_width='1920';resolution_scale='1';ht_render_quality='1';present_effect='"fsr"';ht_perf_log='true';log_level='"warn"'}
foreach($pair in ($Set -split ';' | Where-Object {$_})){$k,$v=$pair -split '=',2;$values[$k.Trim()]=$v.Trim()}
$config=Join-Path $sandbox 'hydrothunder.toml'
$original=[System.IO.File]::ReadAllText($config);$text=$original
foreach($key in $values.Keys){$line="$key = $($values[$key])";if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}}
[System.IO.File]::WriteAllText($config,$text)
[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),'0 0 0 0 0 0 0 0')
$logDir=Join-Path $sandbox 'logs';$known=@(Get-ChildItem $logDir -Filter '*.log' | ForEach-Object Name)
$env:HT_BOOT_TEST='1';$env:HT_SETTINGS_ROOT=$settings;$env:HT_TEST_CVARS=$Cvars
Add-Type -Namespace Ht -Name Th -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern System.IntPtr OpenThread(int access, bool inherit, int id);
[DllImport("kernel32.dll")] public static extern int GetThreadDescription(System.IntPtr h, out System.IntPtr desc);
[DllImport("kernel32.dll")] public static extern bool CloseHandle(System.IntPtr h);
[DllImport("kernel32.dll")] public static extern System.IntPtr LocalFree(System.IntPtr p);
[DllImport("ntdll.dll")] public static extern int NtQueryInformationThread(System.IntPtr h, int cls, out System.IntPtr info, int len, System.IntPtr ret);
public static long Start(int id) {
  System.IntPtr h = OpenThread(0x0040, false, id); if (h == System.IntPtr.Zero) return 0;
  System.IntPtr a; int r = NtQueryInformationThread(h, 9, out a, System.IntPtr.Size, System.IntPtr.Zero);
  CloseHandle(h); return r == 0 ? a.ToInt64() : 0;
}
public static string Name(int id) {
  System.IntPtr h = OpenThread(0x1000, false, id); if (h == System.IntPtr.Zero) return "";
  System.IntPtr d; string n = "";
  if (GetThreadDescription(h, out d) >= 0 && d != System.IntPtr.Zero) { n = System.Runtime.InteropServices.Marshal.PtrToStringUni(d); LocalFree(d); }
  CloseHandle(h); return n;
}
'@
function HtWhere([long]$a){ if(!$a){return ''}; foreach($m in $p.Modules){ $b=$m.BaseAddress.ToInt64(); if($a -ge $b -and $a -lt $b+$m.ModuleMemorySize){ return ('{0}+0x{1:X}' -f $m.ModuleName,($a-$b)) } }; return ('0x{0:X}' -f $a) }
$rows=@()
try{
  $p=Start-Process (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -PassThru
  Start-Sleep $Warmup
  $p.Refresh();$t0=@{};foreach($th in $p.Threads){try{$t0[$th.Id]=$th.TotalProcessorTime.TotalMilliseconds}catch{}}
  $proc0=$p.TotalProcessorTime.TotalMilliseconds;$w0=Get-Date
  Start-Sleep $Window
  $p.Refresh();$secs=((Get-Date)-$w0).TotalSeconds
  foreach($th in $p.Threads){try{$ms=$th.TotalProcessorTime.TotalMilliseconds;if($t0.ContainsKey($th.Id)){$rows+=[pscustomobject]@{Thread=$th.Id;CpuMsPerSec=[math]::Round(($ms-$t0[$th.Id])/$secs,1);Name=[Ht.Th]::Name($th.Id);Start=(HtWhere ([Ht.Th]::Start($th.Id)))}}}catch{}}
  $procMs=[math]::Round(($p.TotalProcessorTime.TotalMilliseconds-$proc0)/$secs,1)
}
finally{if($p -and !$p.HasExited){$p.CloseMainWindow()|Out-Null;Start-Sleep 4;if(!$p.HasExited){Stop-Process -Id $p.Id -Force}};[System.IO.File]::WriteAllText($config,$original)}
$log=Get-ChildItem $logDir -Filter '*.log' | Where-Object {$known -notcontains $_.Name} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
"Log: $($log.Name)   processo inteiro: $procMs ms de CPU por segundo (1000 = 1 nucleo cheio)"
$rows | Sort-Object CpuMsPerSec -Descending | Select-Object -First 10 | Format-Table -AutoSize | Out-String
Get-Content $log.FullName | Where-Object {$_ -match '\[ht-cpu\]|\[ht-draw\]'} | Select-Object -Last 2
