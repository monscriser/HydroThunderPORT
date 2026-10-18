# Keyboard-only race (real key events, NO HT_BOOT_TEST virtual pad): proves the keybind_*
# keys reach the game. Opens the isolated game-test copy with -Settings (its toml holds the
# binds), passes the menus with the confirm key, then holds throttle, steers, brakes and
# jumps, taking screenshots (speedometer) before/during/after each action.
# Usage: kb_race.ps1 -Settings <settings dir> -Out <folder> [-Throttle E -Brake Q -Jump X -Confirm Space -Start Return]
param([Parameter(Mandatory)][string]$Settings,[Parameter(Mandatory)][string]$Out,
      [string]$Throttle='E',[string]$Brake='Q',[string]$Jump='X',[string]$Confirm='Space',[string]$Start='Return',
      [int]$MenuSeconds=75)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
if (-not ('HtKb' -as [type])) {
Add-Type @"
using System;using System.Runtime.InteropServices;
public static class HtKb {
  [StructLayout(LayoutKind.Sequential)] struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit, Size = 40)] struct IN { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KI ki; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, IN[] i, int s);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int p);
  public static bool Front(int pid){int p;GetWindowThreadProcessId(GetForegroundWindow(),out p);return p==pid;}
  public static void Key(ushort vk, bool up, bool ext) {
    var i = new IN[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.scan = (ushort)MapVirtualKey(vk, 0);
    i[0].ki.flags = (up ? 2u : 0u) | (ext ? 1u : 0u); SendInput(1, i, Marshal.SizeOf(typeof(IN)));
  }
}
"@
}
# Engine key names -> Windows virtual keys (subset used by the binds).
function Vk([string]$n){
  switch($n){'Space'{0x20}'Return'{0x0D}'Backspace'{0x08}'Tab'{0x09}'Left'{0x25}'Up'{0x26}'Right'{0x27}'Down'{0x28}
    default{ if($n.Length -eq 1){[int][char]$n.ToUpper()} else {throw "unknown key $n"} } }
}
function Ext([string]$n){ @('Left','Up','Right','Down') -contains $n }
function Down([string]$n){[HtKb]::Key([uint16](Vk $n),$false,(Ext $n))}
function Up([string]$n){[HtKb]::Key([uint16](Vk $n),$true,(Ext $n))}
function Tap([string]$n,[int]$ms=120){Down $n;Start-Sleep -Milliseconds $ms;Up $n}
function Shot([string]$name){$b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds;$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height;$g=[System.Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size);$bmp.Save((Join-Path $Out "$name.png"),[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$bmp.Dispose()}
$script:unfocused=0
function Keep($p){if(![HtKb]::Front($p.Id)){$script:unfocused++;$p.Refresh();[void][HtKb]::SetForegroundWindow($p.MainWindowHandle);Start-Sleep -Milliseconds 200}}

$env:HT_BOOT_TEST='';$env:HT_TEST_CVARS='';$env:HT_SETTINGS_ROOT=$Settings
$log=@()
try{
  $p=Start-Process -FilePath (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -PassThru
  Start-Sleep 15
  # Menus: confirm every ~1.25 s, Start only in the first taps (a late Start pauses the race).
  $end=(Get-Date).AddSeconds($MenuSeconds);$i=0
  while((Get-Date) -lt $end -and !$p.HasExited){Keep $p;if($i%4 -eq 3 -and $i -lt 28){Tap $Start}else{Tap $Confirm};Start-Sleep -Milliseconds 1100;$i++}
  Keep $p
  # Race: hold throttle, short steering taps; then brake, then jump.
  Down $Throttle
  $t0=Get-Date
  function Wait([double]$until){while(((Get-Date)-$t0).TotalSeconds -lt $until -and !$p.HasExited){$s=[int](((Get-Date)-$t0).TotalSeconds);if($s%6 -eq 1){Tap 'Left' 150}elseif($s%6 -eq 4){Tap 'Right' 150};Start-Sleep -Milliseconds 400}}
  # Straight run from the start (no steering, so the boat does not hit a wall before
  # the brake test): 5 s after the countdown the speedometer is already high.
  while(((Get-Date)-$t0).TotalSeconds -lt 10 -and !$p.HasExited){Start-Sleep -Milliseconds 200}
  Keep $p; Shot '01-acelerando'; $log+="01 acelerando $(Get-Date -f HH:mm:ss.fff)"
  # Control: only release the throttle for 1.5 s (coasting also slows the boat).
  Up $Throttle; Start-Sleep -Milliseconds 1500; Shot '02-solto-1.5s'
  Down $Throttle; Start-Sleep -Milliseconds 5000; Shot '03-acelerando-antes-do-freio'
  # Brake: release throttle and hold the brake for the same 1.5 s.
  Up $Throttle; Down $Brake; Start-Sleep -Milliseconds 1500; Shot '04-freando-1.5s'; Up $Brake
  $log+="02 solto / 04 freio $Brake"
  Down $Throttle; Wait 30; Keep $p; Shot '05-acelerando-de-novo'
  # Jump: tap jump several times (needs boost meter; screenshots right after).
  foreach($n in 1..4){Tap $Jump 150;Start-Sleep -Milliseconds 250;Shot ("06-pulo-$n");Start-Sleep -Milliseconds 1500}
  $log+="05 pulo $Jump"
  Wait 50
  Up $Throttle
}finally{
  foreach($k in @($Throttle,$Brake,$Jump)){try{Up $k}catch{}}
  if($p -and !$p.HasExited){$p.CloseMainWindow()|Out-Null;Start-Sleep 4;if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
}
$log+="SemFoco=$script:unfocused"
$log | Set-Content (Join-Path $Out 'roteiro.txt')
$log
