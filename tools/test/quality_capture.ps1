param(
  [Parameter(Mandatory)][string]$GameExe,
  [Parameter(Mandatory)][string]$OutDir,
  [string]$FullShots = '6,14',
  [string]$Only = ''
)
# Same race moment, 1080p borderless, captured losslessly for each upscaling
# path, so image quality can be compared side by side and numerically.
$ErrorActionPreference = 'Stop'
$toml = Join-Path $env:APPDATA 'HydroThunderRecompiled\hydrothunder.toml'
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $toml (Join-Path $OutDir 'hydrothunder.toml.before-quality') -Force
$utf8 = New-Object Text.UTF8Encoding $false
function SetKeys([hashtable]$kv) {
  $text = [IO.File]::ReadAllText($toml)
  foreach ($k in $kv.Keys) {
    $line = "$k = $($kv[$k])"
    if ($text -match "(?m)^$k\s*=") { $text = [regex]::Replace($text, "(?m)^$k\s*=.*$", $line) }
    else { $text = $line + "`n" + $text }
  }
  [IO.File]::WriteAllText($toml, $text, $utf8)
}
# Sharp = FSR 3.1 built-in sharpening (0 = off); Rcas = RCAS sharpness reduction
# in stops (lower = sharper; 0.2 is the engine default).
$all = @(
  @{ Name = 'nativa_2x';  Scale = 2; Effect = 'fsr3'; Sharp = 0.0; Rcas = 0.2 },
  @{ Name = 'fsr3';       Scale = 1; Effect = 'fsr3'; Sharp = 0.0; Rcas = 0.2 },
  @{ Name = 'fsr1';       Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 0.2 },
  @{ Name = 'bilinear';   Scale = 1; Effect = 'bilinear'; Sharp = 0.0; Rcas = 0.2 },
  @{ Name = 'fsr3_s05';   Scale = 1; Effect = 'fsr3'; Sharp = 0.5; Rcas = 0.2 },
  @{ Name = 'fsr3_s10';   Scale = 1; Effect = 'fsr3'; Sharp = 1.0; Rcas = 0.2 },
  @{ Name = 'fsr3_rcas0'; Scale = 1; Effect = 'fsr3'; Sharp = 0.0; Rcas = 0.0 },
  @{ Name = 'fsr3_s10_rcas0'; Scale = 1; Effect = 'fsr3'; Sharp = 1.0; Rcas = 0.0 },
  @{ Name = 'fsr1_n0';  Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 2.0 },
  @{ Name = 'fsr1_n7';  Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 0.25 },
  @{ Name = 'fsr1_n10'; Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 0.0 },
  @{ Name = 'fsr1_n7_semfxaa'; Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 0.25; Fxaa = '' },
  @{ Name = 'fsr1_n0_semfxaa'; Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 2.0; Fxaa = '' },
  @{ Name = 'bilinear_semfxaa'; Scale = 1; Effect = 'bilinear'; Sharp = 0.0; Rcas = 0.25; Fxaa = '' },
  @{ Name = 'fsr1_n10_semfxaa'; Scale = 1; Effect = 'fsr'; Sharp = 0.0; Rcas = 0.0; Fxaa = '' },
  @{ Name = 'nativa_semfxaa'; Scale = 2; Effect = 'fsr'; Sharp = 0.0; Rcas = 0.25; Fxaa = '' }
)
$configs = if ($Only) { $all | Where-Object { $Only -split ',' -contains $_.Name } } else { $all[0..3] }
foreach ($c in $configs) {
  SetKeys @{
    present_effect = "`"$($c.Effect)`""; resolution_scale = "$($c.Scale)"
    window_width = '1920'; window_height = '1080'; ht_output_width = '1920'
    ht_game_fullscreen = 'true'; fullscreen = 'true'; fullscreen_mode = '"borderless"'
    ht_show_fps = 'false'; swap_post_effect = $(if ($c.ContainsKey('Fxaa')) { '"' + $c.Fxaa + '"' } else { '"fxaa"' })
    present_fsr3_sharpness = ([double]$c.Sharp).ToString('0.0#', [Globalization.CultureInfo]::InvariantCulture); present_fsr_sharpness_reduction = ([double]$c.Rcas).ToString('0.0#', [Globalization.CultureInfo]::InvariantCulture)
  }
  $r = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'drive_race.ps1') `
    -GameExe $GameExe -Idle -RaceSeconds 26 -ShotPrefix (Join-Path $OutDir $c.Name) -FullShots $FullShots
  "$($c.Name): $r"
}
Copy-Item (Join-Path $OutDir 'hydrothunder.toml.before-quality') $toml -Force
