param([string]$Keys = '', [string]$Shot = '', [int]$DelayMs = 900, [string]$Click = '', [string]$Process = 'hydrothunder', [string]$Scroll = '')
# Sends a comma-separated key list (SPACE, ENTER, UP, DOWN, LEFT, RIGHT, BACK, E, A, D)
# to the running game window, then optionally saves a screenshot of it.
Add-Type -AssemblyName System.Drawing
if (-not ('HtK' -as [type]) -or -not ([HtK].GetMethod('Wheel'))) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class HtK {
  [StructLayout(LayoutKind.Sequential)] struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Explicit, Size = 40)] struct IN { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KI ki; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] static extern uint SendInput(uint n, IN[] i, int s);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint c, uint t);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint a, uint b, bool f);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  public static void Wheel(int x, int y, int n) { SetCursorPos(x, y); System.Threading.Thread.Sleep(80); for (int i = 0; i < System.Math.Abs(n); i++) { mouse_event(0x0800, 0, 0, (uint)(n < 0 ? -120 : 120), IntPtr.Zero); System.Threading.Thread.Sleep(60); } }
  public static void ClickAt(int x, int y) { SetCursorPos(x, y); System.Threading.Thread.Sleep(80); mouse_event(2, 0, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(60); mouse_event(4, 0, 0, 0, IntPtr.Zero); }
  public static void Key(ushort vk, bool up, bool ext) {
    var i = new IN[1]; i[0].type = 1; i[0].ki.vk = vk; i[0].ki.scan = (ushort)MapVirtualKey(vk, 0);
    i[0].ki.flags = (up ? 2u : 0u) | (ext ? 1u : 0u); SendInput(1, i, Marshal.SizeOf(typeof(IN)));
  }
  public static bool Focus(IntPtr h, uint pid) {
    for (int n = 0; n < 5; n++) {
      uint p; GetWindowThreadProcessId(GetForegroundWindow(), out p); if (p == pid) return true;
      uint d; uint fg = GetWindowThreadProcessId(GetForegroundWindow(), out d); uint me = GetCurrentThreadId();
      AttachThreadInput(me, fg, true); Key(0x12, false, false); Key(0x12, true, false);
      BringWindowToTop(h); SetForegroundWindow(h); AttachThreadInput(me, fg, false);
      System.Threading.Thread.Sleep(150);
    }
    return false;
  }
}
"@
}
$map = @{ SPACE = 0x20; ENTER = 0x0D; UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27; BACK = 0x08; E = 0x45; A = 0x41; D = 0x44; W = 0x57; S = 0x53 }
$ext = @('UP','DOWN','LEFT','RIGHT')
$p = Get-Process $Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { 'NO_GAME'; exit 1 }
if (-not [HtK]::Focus($p.MainWindowHandle, [uint32]$p.Id)) { 'NO_FOCUS'; exit 1 }
if ($Click) {
  # Coordinates are in the 960-px-wide screenshot of the window.
  $r0 = New-Object HtK+RECT; [HtK]::GetWindowRect($p.MainWindowHandle, [ref]$r0) | Out-Null
  $sc = ($r0.R - $r0.L) / 960.0
  foreach ($c in ($Click -split ';' | Where-Object { $_ })) { $xy = $c -split ','; [HtK]::ClickAt([int]($r0.L + [int]$xy[0] * $sc), [int]($r0.T + [int]$xy[1] * $sc)); Start-Sleep -Milliseconds $DelayMs }
}
if ($Scroll) {
  $r1 = New-Object HtK+RECT; [HtK]::GetWindowRect($p.MainWindowHandle, [ref]$r1) | Out-Null
  $s1 = ($r1.R - $r1.L) / 960.0; $v = $Scroll -split ','
  [HtK]::Wheel([int]($r1.L + [int]$v[0] * $s1), [int]($r1.T + [int]$v[1] * $s1), [int]$v[2]); Start-Sleep -Milliseconds $DelayMs
}
foreach ($k in ($Keys -split ',' | Where-Object { $_ })) {
  $name = $k.Trim().ToUpper()
  [HtK]::Key([uint16]$map[$name], $false, $ext -contains $name); Start-Sleep -Milliseconds 90
  [HtK]::Key([uint16]$map[$name], $true, $ext -contains $name); Start-Sleep -Milliseconds $DelayMs
}
if ($Shot) {
  $r = New-Object HtK+RECT; [HtK]::GetWindowRect($p.MainWindowHandle, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $h = $r.B - $r.T
  $bmp = New-Object Drawing.Bitmap $w, $h; $g = [Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $small = New-Object Drawing.Bitmap $bmp, 960, ([int]($h * 960 / $w))
  $small.Save($Shot, [Drawing.Imaging.ImageFormat]::Jpeg); $small.Dispose(); $g.Dispose(); $bmp.Dispose()
  "SHOT $Shot"
}
