param(
  [Parameter(Mandatory)][string]$GameExe,
  [Parameter(Mandatory)][string]$OutDir,
  [string[]]$Outputs = @('720p','1080p','1440p','2160p'),
  [int[]]$Qualities = @(0,1,2,3,4),
  [string]$Effect = 'fsr3',
  [string]$Rcas = '0.2',
  [string]$Fsr3Sharp = '0.0',
  [int]$RaceSeconds = 180,
  [int]$SkipSeconds = 60,
  [string]$FullShots = '9999'
)
# Runs one automated race per (output resolution x render quality) and records,
# from the game's own log, whether FSR 3.1 ran, at which sizes, and FPS/GPU load.
$ErrorActionPreference = 'Stop'
$cfgDir = Join-Path $env:APPDATA 'HydroThunderRecompiled'
$toml = Join-Path $cfgDir 'hydrothunder.toml'
$logDir = Join-Path $cfgDir 'logs'
New-Item -ItemType Directory -Force $OutDir | Out-Null
Copy-Item $toml (Join-Path $OutDir 'hydrothunder.toml.before-matrix') -Force
$utf8 = New-Object Text.UTF8Encoding $false
$inv = [Globalization.CultureInfo]::InvariantCulture
$sizes = @{ '720p' = @(1280,720); '1080p' = @(1920,1080); '1440p' = @(2560,1440); '2160p' = @(3840,2160) }
$ratios = @(1.0,1.5,1.7,2.0,3.0)
$names = @('Nativa','Qualidade','Equilibrado','Desempenho','Ultra desempenho')

# Same rule as RenderScale() in ht_setup_wizard.cpp.
function RenderScale([int]$q, [int]$h) {
  if ($q -le 0) { return [math]::Min(3, [math]::Max(1, [math]::Floor(($h + 719) / 720))) }
  $t = $h / $ratios[$q] / 720.0
  return [math]::Min(3, [math]::Max(1, [math]::Floor($t + 0.49)))
}
function SetKeys([hashtable]$kv) {
  $text = [IO.File]::ReadAllText($toml)
  foreach ($k in $kv.Keys) {
    $line = "$k = $($kv[$k])"
    if ($text -match "(?m)^$k\s*=") { $text = [regex]::Replace($text, "(?m)^$k\s*=.*$", $line) }
    else { $text = $line + "`n" + $text }
  }
  [IO.File]::WriteAllText($toml, $text, $utf8)
}

$rows = @()
foreach ($o in $Outputs) {
  foreach ($q in $Qualities) {
    $w, $h = $sizes[$o]
    $scale = RenderScale $q $h
    # 1080p on a 1080p panel runs borderless like a player would; other sizes
    # use a window of exactly the output size.
    $borderless = ($o -eq '1080p')
    SetKeys @{
      present_effect = "`"$Effect`""; resolution_scale = "$scale"; ht_render_quality = "$q"
      window_width = "$w"; window_height = "$h"; ht_output_width = "$w"
      ht_game_fullscreen = $(if ($borderless) {'true'} else {'false'})
      fullscreen = $(if ($borderless) {'true'} else {'false'})
      fullscreen_mode = '"borderless"'; ht_perf_log = 'true'
      present_fsr_sharpness_reduction = $Rcas; present_fsr3_sharpness = $Fsr3Sharp; ht_show_fps = 'true'
    }
    $before = (Get-ChildItem $logDir -Filter 'hydrothunder_*.log' | Measure-Object).Count
    $samples = Join-Path $OutDir "gpu_${o}_q$q.csv"
    $job = Start-Job -ArgumentList $samples -ScriptBlock {
      param($out)
      $inv = [Globalization.CultureInfo]::InvariantCulture
      "t,total" | Out-File $out -Encoding ascii
      while ($true) {
        try {
          $s = (Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -ErrorAction Stop).CounterSamples
          [string]::Format($inv, "{0:HH:mm:ss},{1:F1}", (Get-Date), ($s | Measure-Object CookedValue -Sum).Sum) | Out-File $out -Append -Encoding ascii
        } catch {}
        Start-Sleep -Milliseconds 700
      }
    }
    $drive = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'drive_race.ps1') -GameExe $GameExe -RaceSeconds $RaceSeconds -ShotPrefix (Join-Path $OutDir "foto_${o}_q$q") -FullShots $FullShots
    Stop-Job $job; Remove-Job $job
    Start-Sleep -Seconds 2
    $log = Get-ChildItem $logDir -Filter 'hydrothunder_*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    Copy-Item $log.FullName (Join-Path $OutDir "log_${o}_q$q.log") -Force
    $lines = Get-Content $log.FullName
    $chain = ($lines | Select-String '\[ht-fsr\].*chain: (.*)$' | Select-Object -Last 1)
    $chain = if ($chain) { $chain.Matches[0].Groups[1].Value } else { '' }
    $active = ($lines | Select-String 'FSR 3\.1 active: .*\((\d+x\d+) -> (\d+x\d+)\)' | Select-Object -Last 1)
    $fsr3 = if ($active) { "$($active.Matches[0].Groups[1].Value)->$($active.Matches[0].Groups[2].Value)" } else { '-' }
    # Measure only the real race: skip menus, tutorial pages and the start
    # countdown (the first $SkipSeconds after the driver starts racing).
    $rs = [regex]::Match(($drive -join ' '), 'race_start=(\d\d:\d\d:\d\d)').Groups[1].Value
    $from = ([datetime]::ParseExact($rs, 'HH:mm:ss', $inv)).AddSeconds($SkipSeconds).ToString('HH:mm:ss')
    function InRace($line) { $m = [regex]::Match($line, '^\[\S+ (\d\d:\d\d:\d\d)'); $m.Success -and $m.Groups[1].Value -ge $from }
    $raceFps = @($lines | Where-Object { $_ -match 'presented fps=' -and (InRace $_) } | ForEach-Object { [double]([regex]::Match($_, 'presented fps=([\d.]+)').Groups[1].Value) })
    $raceBusy = @($lines | Where-Object { $_ -match 'guest gpu busy' -and (InRace $_) } | ForEach-Object { [int]([regex]::Match($_, 'guest gpu busy (\d+)%').Groups[1].Value) })
    $gpu = @(Import-Csv $samples | Where-Object { $_.t -ge $from } | ForEach-Object { [double]::Parse($_.total, $inv) })
    $crash = ($drive -match 'crashed=True') -or ($lines -match 'VEH stack|htrecomp crash')
    $rows += [pscustomobject]@{
      Saida = $o; Qualidade = $names[$q]; Escala = "${scale}x"; Interna = "$(1280*$scale)x$(720*$scale)"
      Cadeia = $chain; FSR3 = $fsr3
      FPS = if ($raceFps) { [math]::Round(($raceFps | Measure-Object -Average).Average, 1) } else { 0 }
      FPSmin = if ($raceFps) { ($raceFps | Measure-Object -Minimum).Minimum } else { 0 }
      GPUjogo = if ($raceBusy) { [math]::Round(($raceBusy | Measure-Object -Average).Average) } else { 0 }
      GPUwin = if ($gpu) { [math]::Round(($gpu | Measure-Object -Average).Average) } else { 0 }
      Corrida = $raceFps.Count -ge 30; Desfoco = [regex]::Match(($drive -join ' '), 'unfocused=(\d+)').Groups[1].Value; Travou = [bool]$crash; Log = $log.Name
    }
    $rows[-1] | Format-List | Out-String | Write-Host
    $rows | Export-Csv (Join-Path $OutDir 'resultado.csv') -NoTypeInformation -Encoding UTF8
  }
}
Copy-Item (Join-Path $OutDir 'hydrothunder.toml.before-matrix') $toml -Force
$rows | Format-Table -AutoSize | Out-String -Width 250
