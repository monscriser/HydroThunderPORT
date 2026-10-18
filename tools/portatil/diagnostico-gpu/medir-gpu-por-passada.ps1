# Diagnostico de GPU por passada (ROG Ally / outros PCs Windows) - Hydro Thunder 8.9.2+.
# Coloque a pasta "diagnostico-gpu" DENTRO da pasta do jogo (onde fica Launcher-Config.exe).
# O script: guarda a DLL normal, poe a DLL de diagnostico, liga o registro de desempenho,
# abre o jogo direto, espera voce jogar e fechar, DEVOLVE a DLL e a configuracao originais
# e junta o log na Area de Trabalho (pasta hydro-diagnostico-gpu).
# Test only: -Jogo <pasta falsa> -SemJogo (pula abrir o jogo).
param([string]$Jogo = '', [switch]$SemJogo)
$ErrorActionPreference = 'Stop'
$kit = $PSScriptRoot
$game = if ($Jogo) { $Jogo } else { Split-Path $kit -Parent }
$data = Join-Path $game 'data'
$exe = Join-Path $data 'hydrothunder.exe'
$dll = Join-Path $data 'rexgpu-xenos.dll'
$diag = Join-Path $kit 'rexgpu-xenos-diagnostico.dll'
if (!(Test-Path $exe) -or !(Test-Path $dll)) {
  Write-Host "Nao achei $exe." -ForegroundColor Red
  Write-Host 'Coloque a pasta diagnostico-gpu dentro da pasta do jogo (ao lado de Launcher-Config.exe).'
  Read-Host 'Enter para sair'; exit 1
}
$root = Join-Path $env:APPDATA 'HydroThunderRecompiled'
$toml = Join-Path $root 'hydrothunder.toml'
$logs = Join-Path $root 'logs'
if (!(Test-Path $toml)) { Write-Host 'Abra o jogo uma vez pelo launcher, feche e rode de novo.' -ForegroundColor Red; Read-Host 'Enter para sair'; exit 1 }

$backupDir = Join-Path $kit 'backup'
New-Item -ItemType Directory -Force $backupDir | Out-Null
$dllBackup = Join-Path $backupDir 'rexgpu-xenos.dll'
if (!(Test-Path $dllBackup)) { Copy-Item $dll $dllBackup }   # never overwrite an earlier backup
$tomlOriginal = [IO.File]::ReadAllText($toml)
[IO.File]::WriteAllText((Join-Path $backupDir 'hydrothunder.toml'), $tomlOriginal)
$known = @(Get-ChildItem $logs -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object Name)
try {
  Copy-Item $diag $dll -Force
  $text = $tomlOriginal
  foreach ($pair in @('ht_perf_log = true', 'log_level = "warn"')) {
    $k = ($pair -split ' = ')[0]
    if ($text -match "(?m)^$k\s*=") { $text = [regex]::Replace($text, "(?m)^$k\s*=.*$", $pair) } else { $text = $pair + "`n" + $text }
  }
  [IO.File]::WriteAllText($toml, $text)
  # Trace one frame every ~10 s (at 120 FPS) from ~50 s to ~5 min after opening.
  $env:HT_TRACE_FRAME = ((6000..36000 | Where-Object { $_ % 1200 -eq 0 }) -join ',')
  Write-Host ''
  Write-Host '=== Diagnostico de GPU do Hydro Thunder ===' -ForegroundColor Cyan
  Write-Host 'O jogo vai abrir sozinho. Jogue UMA corrida inteira em Asgard (pista 2),'
  Write-Host 'acelerando e virando, com as configuracoes de sempre (720p, FSR 1, sem limite).'
  Write-Host 'No fim da corrida, feche o jogo. Esta janela termina sozinha.'
  if (!$SemJogo) { Start-Process -FilePath $exe -WorkingDirectory $data -ArgumentList '--ht_direct_launch=true' -Wait }
} finally {
  $env:HT_TRACE_FRAME = ''
  Copy-Item $dllBackup $dll -Force
  [IO.File]::WriteAllText($toml, $tomlOriginal)
  Write-Host 'DLL e configuracao originais devolvidas.' -ForegroundColor Green
}
$log = Get-ChildItem $logs -Filter '*.log' -ErrorAction SilentlyContinue | Where-Object { $known -notcontains $_.Name } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) 'hydro-diagnostico-gpu'
New-Item -ItemType Directory -Force $out | Out-Null
if ($log) {
  Copy-Item $log.FullName (Join-Path $out $log.Name)
  $traced = @(Select-String -Path $log.FullName -Pattern '\[ht-trace\] ===== frame end, draws=(\d+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
  "Quadros rastreados: $($traced.Count) (desenhos: $($traced -join ', '))" | Set-Content (Join-Path $out 'resumo.txt')
  Write-Host "Pronto. Mande a pasta $out (zipada) para o Claude." -ForegroundColor Cyan
} else {
  Write-Host 'Nao achei um log novo. O jogo abriu?' -ForegroundColor Red
}
Read-Host 'Enter para sair'
