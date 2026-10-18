# Real Lossless Scaling frame-generation check inside a race (Windows).
# Run it OUTSIDE packaged apps (Codex/Claude desktop), e.g. through
# lossless_race.cmd opened by explorer.exe; otherwise MSIX virtualizes
# %LOCALAPPDATA% and LS reads a different Settings.xml.
# Uses only the isolated copy outputs/_teste_modern/game-test.
param([int]$Multiplier=2,[int]$AboveRefresh=0,[int]$ShowLsCounter=0,[int]$MenuSeconds=75,[int]$RaceSeconds=70)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
$out=Join-Path $workspace ('outputs/_teste_modern/lsfg-race-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $out | Out-Null
# Persistent like a real install: keeps lossless-original-sync.xml between runs,
# so turning above-refresh off restores the previous LS sync values.
$settings=Join-Path $workspace 'outputs/_teste_modern/lsfg-race-settings'
New-Item -ItemType Directory -Force $settings | Out-Null
$log=Join-Path $out 'race.log'
function Note([string]$m){ "$(Get-Date -Format 'HH:mm:ss.fff') $m" | Tee-Object -FilePath $log -Append }
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class HtWin{[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")]public static extern int GetWindowThreadProcessId(IntPtr h,out int pid);
delegate bool P(IntPtr h,IntPtr l);[DllImport("user32.dll")]static extern bool EnumWindows(P p,IntPtr l);[DllImport("user32.dll")]static extern bool IsWindowVisible(IntPtr h);[DllImport("user32.dll")]static extern bool IsIconic(IntPtr h);
[DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
public static string Visible(int pid){var r=new System.Collections.Generic.List<string>();EnumWindows((h,l)=>{int p;GetWindowThreadProcessId(h,out p);if(p==pid&&IsWindowVisible(h)){var s=new System.Text.StringBuilder(256);GetWindowText(h,s,256);r.Add("'"+s+"' minimized="+IsIconic(h));}return true;},IntPtr.Zero);return string.Join("; ",r);}}
'@
function Shot([string]$name){
  $b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height
  $g=[System.Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size)
  $bmp.Save((Join-Path $out "$name.png"),[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$bmp.Dispose()
  $fg=[HtWin]::GetForegroundWindow();$fpid=0;[void][HtWin]::GetWindowThreadProcessId($fg,[ref]$fpid)
  $fname=(Get-Process -Id $fpid -ErrorAction SilentlyContinue).ProcessName
  Note "shot $name foreground=$fname"
}
function Pad([int]$buttons,[int]$rt,[int]$lx){[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),"$buttons $rt 0 0 $lx 0 0 0")}

$lsCfg=Join-Path $env:LOCALAPPDATA 'Lossless Scaling\Settings.xml'
Note "LOCALAPPDATA=$env:LOCALAPPDATA settings_exists=$(Test-Path -LiteralPath $lsCfg)"
if(Test-Path -LiteralPath $lsCfg){Copy-Item -LiteralPath $lsCfg (Join-Path $out 'ls-settings-before.xml')}
# An LS left in the tray by a previous session is closed and restarted by the game.
Note "lossless open before launch=$([bool](Get-Process LosslessScaling -ErrorAction SilentlyContinue))"

# Isolated game config: windowed borderless fullscreen 1280x720, 60 FPS cap, FSR1.
$config=Join-Path $sandbox 'hydrothunder.toml'
$original=[System.IO.File]::ReadAllText($config)
$text=$original
$values=@{fps_limit='60';video_mode_refresh_rate='60.0';vsync='true';ht_import_on_start='false';ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';ht_perf_log='true';ht_show_fps='true';log_level='"warn"'}
foreach($key in $values.Keys){$line="$key = $($values[$key])";if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}}
[System.IO.File]::WriteAllText($config,$text)
Note "multiplier=$Multiplier above_refresh=$AboveRefresh"
@('HT-LS-2','1',"$Multiplier","$AboveRefresh",'0') | Set-Content -LiteralPath (Join-Path $settings 'lossless-scaling.txt') -Encoding ASCII
Pad 0 0 0
$env:HT_BOOT_TEST='1';$env:HT_SETTINGS_ROOT=$settings;$env:HT_TEST_LS_COUNTER="$ShowLsCounter";Note "show_ls_counter=$ShowLsCounter"
try{
  $p=Start-Process -FilePath (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -PassThru
  Note "game pid=$($p.Id)"
  Start-Sleep 15
  [void][HtWin]::SetForegroundWindow($p.MainWindowHandle)
  $ls=Get-Process LosslessScaling -ErrorAction SilentlyContinue
  Note "lossless running=$([bool]$ls) $($ls.Path)"; foreach($x in @($ls)){Note "lossless visible windows pid $($x.Id): [$([HtWin]::Visible($x.Id))]"}
  Shot '01-boot'
  # Menus + tutorial: alternate A and Start pulses.
  $end=(Get-Date).AddSeconds($MenuSeconds);$i=0
  while((Get-Date) -lt $end -and !$p.HasExited){
    Pad $(if($i%4 -eq 3){16}else{4096}) 0 0;Start-Sleep -Milliseconds 150;Pad 0 0 0;Start-Sleep -Milliseconds 1100;$i++
    if($i%12 -eq 0){[void][HtWin]::SetForegroundWindow($p.MainWindowHandle);Shot ('02-menu-'+$i)}
  }
  # Race: hold RT, short steering taps.
  $end=(Get-Date).AddSeconds($RaceSeconds);$t=0
  while((Get-Date) -lt $end -and !$p.HasExited){
    $lx=switch($t%6){1{-20000}4{20000}default{0}}
    Pad 0 255 $lx;Start-Sleep -Milliseconds 400;$t++
    if($t%25 -eq 0){Shot ('03-race-'+$t)}
  }
  Shot '04-end'
  foreach($x in @(Get-Process LosslessScaling -ErrorAction SilentlyContinue)){Note "lossless visible windows at end pid $($x.Id): [$([HtWin]::Visible($x.Id))]"}
  if(Test-Path -LiteralPath $lsCfg){Copy-Item -LiteralPath $lsCfg (Join-Path $out 'ls-settings-after.xml')}
  Get-ChildItem -LiteralPath $settings | ForEach-Object { Copy-Item $_.FullName (Join-Path $out ('settings-'+$_.Name)) }
}finally{
  Pad 0 0 0
  if($p -and !$p.HasExited){$p.CloseMainWindow()|Out-Null;Start-Sleep 5;if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
  [System.IO.File]::WriteAllText($config,$original)
  Note "done; game config restored. Output=$out"
}
