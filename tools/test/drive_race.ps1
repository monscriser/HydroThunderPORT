param(
  [Parameter(Mandatory)][string]$GameExe,
  [int]$Track = 0,
  [int]$RaceSeconds = 50,
  [string]$ShotPrefix = '',
  [string]$FullShots = '',
  [switch]$Idle
)
# Starts the game, skips menus with Space, then races: holds E (throttle) and
# alternates A / D (steer) so benchmarks include turning, not just a straight run.
# Keys only go out while the game window is in front; screenshots prove the race.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HtInput {
  [StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit, Size = 40)] struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint mapType);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hwnd);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr hwnd, int cmd);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
  public static void Key(ushort vk, bool up) {
    var i = new INPUT[1];
    i[0].type = 1; i[0].ki.wVk = vk; i[0].ki.wScan = (ushort)MapVirtualKey(vk, 0);
    // Arrow keys are extended keys; without the flag their scan code reads as the keypad.
    i[0].ki.dwFlags = (up ? 2u : 0u) | (vk >= 0x25 && vk <= 0x28 ? 1u : 0u);
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
  public static uint ForegroundPid() { uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return pid; }
  // Windows refuses SetForegroundWindow from background processes; attaching to
  // the current foreground thread and tapping Alt lifts that restriction.
  public static void Focus(IntPtr hwnd) {
    uint dummy; uint fg = GetWindowThreadProcessId(GetForegroundWindow(), out dummy);
    uint me = GetCurrentThreadId();
    AttachThreadInput(me, fg, true);
    Key(0x12, false); Key(0x12, true);
    ShowWindow(hwnd, 5); BringWindowToTop(hwnd); SetForegroundWindow(hwnd);
    AttachThreadInput(me, fg, false);
  }
}
"@
$VK_SPACE = 0x20; $VK_E = 0x45; $VK_A = 0x41; $VK_D = 0x44; $VK_LEFT = 0x25; $VK_RIGHT = 0x27
$script:unfocused = 0

function Ensure($p) {
  $p.Refresh()
  if ($p.HasExited) { return $false }
  for ($i = 0; $i -lt 5; $i++) {
    if ([HtInput]::ForegroundPid() -eq $p.Id) { return $true }
    [HtInput]::Focus($p.MainWindowHandle); Start-Sleep -Milliseconds 150
  }
  $script:unfocused++
  return $false
}
function Tap($vk, $ms = 80) { [HtInput]::Key($vk, $false); Start-Sleep -Milliseconds $ms; [HtInput]::Key($vk, $true) }
function Shot($p, $tag, [switch]$Full) {
  if (-not $ShotPrefix) { return }
  $r = New-Object HtInput+RECT
  [HtInput]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
  $w = [math]::Max(1, $r.Right - $r.Left); $h = [math]::Max(1, $r.Bottom - $r.Top)
  $bmp = New-Object Drawing.Bitmap $w, $h
  $g = [Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
  if ($Full) { $bmp.Save("${ShotPrefix}_$tag.png", [Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose(); return }
  # Keep proof images small: scale to 960 px wide.
  $sw = 960; $sh = [int]($h * $sw / $w)
  $small = New-Object Drawing.Bitmap $bmp, $sw, $sh
  $small.Save("${ShotPrefix}_$tag.jpg", [Drawing.Imaging.ImageFormat]::Jpeg)
  $small.Dispose(); $g.Dispose(); $bmp.Dispose()
}

$proc = Start-Process -FilePath $GameExe -ArgumentList '--ht_direct_launch=true' -WorkingDirectory (Split-Path $GameExe) -PassThru
$deadline = (Get-Date).AddSeconds(30)
do { Start-Sleep -Milliseconds 500; $proc.Refresh() } until ($proc.HasExited -or $proc.MainWindowHandle -ne [IntPtr]::Zero -or (Get-Date) -gt $deadline)
if ($proc.HasExited) { "EXITED_EARLY code=$($proc.ExitCode)"; exit 1 }
Start-Sleep -Seconds 2

# Menus, checked with screenshots on the player's save: logos/title -> single
# player (Race already highlighted) -> track select -> boat -> race.
# Track 2 (Invasao de Asgard): Left several times reaches the first track whether
# or not the list wraps (two tracks unlocked), then Right once.
Start-Sleep -Seconds 6
foreach ($i in 1..6) { if (Ensure $proc) { Tap $VK_SPACE }; Start-Sleep -Milliseconds 1700 }
Shot $proc 'menu_pista'
if ($Track -eq 2) {
  foreach ($i in 1..5) { if (Ensure $proc) { Tap $VK_LEFT }; Start-Sleep -Milliseconds 500 }
  if (Ensure $proc) { Tap $VK_RIGHT }; Start-Sleep -Milliseconds 800
}
Shot $proc 'pista_escolhida'
foreach ($i in 1..2) { if (Ensure $proc) { Tap $VK_SPACE }; Start-Sleep -Milliseconds 2000 }
Shot $proc 'inicio'

# Race: hold throttle, weave left and right.
$raceStart = Get-Date
$end = $raceStart.AddSeconds($RaceSeconds)
$left = $true; $shots = @{}; foreach ($s in 30, 90) { if ($s -lt $RaceSeconds - 10) { $shots[$s] = 'corrida{0:000}s' -f $s } }; $shots[[int]$RaceSeconds - 10] = 'corrida_final'; $full = @{}; foreach ($s in ($FullShots -split '[, ]+' | Where-Object { $_ } | ForEach-Object { [int]$_ })) { $full[$s] = 'full{0:000}s' -f $s }
try {
  while ((Get-Date) -lt $end) {
    if ($proc.HasExited) { break }
    if (-not (Ensure $proc)) { Start-Sleep -Milliseconds 300; continue }
    if ($Idle) {
      # Image-quality captures: stay on the start line so every run frames the same scene.
      Start-Sleep -Milliseconds 500
      $t = [int]((Get-Date) - $raceStart).TotalSeconds
      foreach ($k in @($full.Keys)) { if ($t -ge $k) { Shot $proc $full[$k] -Full; $full.Remove($k) } }
      continue
    }
    [HtInput]::Key($VK_E, $false)
    # Space = A: advances the tutorial pages that open the first race; in the race it is boost.
    Tap $VK_SPACE 60
    # Mostly straight at full throttle, with a short arrow-key correction now and then.
    Start-Sleep -Milliseconds 4000
    $vk = if ($left) { $VK_LEFT } else { $VK_RIGHT }
    if (Ensure $proc) { [HtInput]::Key($vk, $false); Start-Sleep -Milliseconds 400; [HtInput]::Key($vk, $true) }
    $left = -not $left
    $t = [int]((Get-Date) - $raceStart).TotalSeconds
    foreach ($k in @($shots.Keys)) { if ($t -ge $k) { Shot $proc $shots[$k]; $shots.Remove($k) } }
    foreach ($k in @($full.Keys)) { if ($t -ge $k) { Shot $proc $full[$k] -Full; $full.Remove($k) } }
  }
} finally {
  [HtInput]::Key($VK_E, $true); [HtInput]::Key($VK_LEFT, $true); [HtInput]::Key($VK_RIGHT, $true)
}
$crashed = $proc.HasExited
if (-not $crashed) { $proc.CloseMainWindow() | Out-Null; if (-not $proc.WaitForExit(8000)) { $proc.Kill() } }
"RACE_DONE crashed=$crashed unfocused=$($script:unfocused) race_start=$($raceStart.ToString('HH:mm:ss'))"
