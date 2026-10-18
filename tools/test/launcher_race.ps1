# Real user flow: the launcher is already open on "Geracao de quadros" with the
# options ticked. Clicks Jogar, passes menus with Space, then races with the
# keyboard (E throttle, A/D steering) and records screenshots and LS windows.
param([string]$Out,[int]$MenuSeconds=75,[int]$RaceSeconds=70)
$ErrorActionPreference='Stop'
$keys=Join-Path $PSScriptRoot 'ht_keys.ps1'
New-Item -ItemType Directory -Force $Out | Out-Null
$log=Join-Path $Out 'race.log'
function Note([string]$m){ "$(Get-Date -Format 'HH:mm:ss.fff') $m" | Tee-Object -FilePath $log -Append }
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class LrWin{delegate bool P(IntPtr h,IntPtr l);[DllImport("user32.dll")]static extern bool EnumWindows(P p,IntPtr l);
[DllImport("user32.dll")]static extern int GetWindowThreadProcessId(IntPtr h,out int pid);[DllImport("user32.dll")]static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")]static extern bool IsIconic(IntPtr h);[DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,System.Text.StringBuilder s,int n);
public static string Visible(int pid){var r=new System.Collections.Generic.List<string>();EnumWindows((h,l)=>{int p;GetWindowThreadProcessId(h,out p);if(p==pid&&IsWindowVisible(h)){var s=new System.Text.StringBuilder(256);GetWindowText(h,s,256);r.Add("'"+s+"' minimized="+IsIconic(h));}return true;},IntPtr.Zero);return string.Join("; ",r);}}
'@
function Shot([string]$name){
  $b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height
  $g=[System.Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size)
  $bmp.Save((Join-Path $Out "$name.png"),[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$bmp.Dispose();Note "shot $name"
}
function LsState([string]$when){
  $ls=@(Get-Process LosslessScaling -ErrorAction SilentlyContinue)
  Note "$when lossless running=$($ls.Count -gt 0) steam running=$([bool](Get-Process steam -ErrorAction SilentlyContinue))"
  foreach($x in $ls){Note "$when lossless visible windows pid $($x.Id): [$([LrWin]::Visible($x.Id))]"}
}
LsState 'before Jogar:'
& $keys -Click '133,654' | Out-Null
Note 'clicked Jogar'
Start-Sleep 15
LsState 'after Jogar:'
Shot '01-boot'
$end=(Get-Date).AddSeconds($MenuSeconds);$i=0
while((Get-Date) -lt $end){
  & $keys -Keys $(if($i%4 -eq 3){'ENTER'}else{'SPACE'}) -DelayMs 1100 | Out-Null;$i++
  if($i%12 -eq 0){Shot ('02-menu-'+$i)}
}
# [HtK] was defined by ht_keys.ps1 in this session: hold E, tap A/D.
$end=(Get-Date).AddSeconds($RaceSeconds);$t=0
[HtK]::Key(0x45,$false,$false)
try{
  while((Get-Date) -lt $end){
    $steer=switch($t%6){1{0x41}4{0x44}default{0}}
    if($steer){[HtK]::Key($steer,$false,$false);Start-Sleep -Milliseconds 250;[HtK]::Key($steer,$true,$false)}else{Start-Sleep -Milliseconds 250}
    Start-Sleep -Milliseconds 150;$t++
    if($t%25 -eq 0){Shot ('03-race-'+$t)}
  }
}finally{[HtK]::Key(0x45,$true,$false)}
LsState 'end of race:'
Shot '04-end'
$cfg=Join-Path $env:LOCALAPPDATA 'Lossless Scaling\Settings.xml'
if(Test-Path -LiteralPath $cfg){Copy-Item -LiteralPath $cfg (Join-Path $Out 'ls-settings-after.xml')}
Note 'done'
