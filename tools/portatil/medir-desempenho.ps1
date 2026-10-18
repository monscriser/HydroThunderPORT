# Medicao de desempenho para ROG Ally / outros PCs Windows (Hydro Thunder revisao 8.6+).
# Liga o registro de desempenho (ht_perf_log) so durante a medicao, espera o usuario
# jogar uma corrida, restaura a configuracao e gera um resumo + copia do log na Area de Trabalho.
# Test only: -Raiz <pasta falsa> -TesteLog <log> (pula as esperas e usa esse log como o da corrida).
param([string]$Raiz = '', [string]$TesteLog = '', [string]$Saida = '')
$ErrorActionPreference = 'Stop'
$root = if ($Raiz) { $Raiz } else { Join-Path $env:APPDATA 'HydroThunderRecompiled' }
$toml = Join-Path $root 'hydrothunder.toml'
$logs = Join-Path $root 'logs'
if (!(Test-Path $toml)) {
  Write-Host "Nao achei $toml." -ForegroundColor Red
  Write-Host 'Abra o jogo uma vez pelo launcher (Launcher-Config.exe), feche, e rode este arquivo de novo.'
  Read-Host 'Enter para sair'; exit 1
}
$backup = "$toml.antes-da-medicao"
Copy-Item $toml $backup -Force
$original = [IO.File]::ReadAllText($toml)
$text = $original
foreach ($pair in @('ht_perf_log = true', 'log_level = "warn"')) {
  $k = ($pair -split ' = ')[0]
  if ($text -match "(?m)^$k\s*=") { $text = [regex]::Replace($text, "(?m)^$k\s*=.*$", $pair) } else { $text = $pair + "`n" + $text }
}
[IO.File]::WriteAllText($toml, $text)
$known = @(Get-ChildItem $logs -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object Name)
Write-Host ''
Write-Host '=== Medicao de desempenho do Hydro Thunder ===' -ForegroundColor Cyan
Write-Host '1. Abra o jogo pelo launcher do jeito de sempre (Jogar).'
Write-Host '2. Jogue UMA corrida inteira em Asgard (pista 2), acelerando e virando.'
Write-Host '3. Feche o jogo.'
Write-Host '4. Volte para esta janela e aperte Enter.'
Write-Host ''
if ($TesteLog) {
  Write-Host "(teste) config durante a medicao:"; Get-Content $toml | Select-Object -First 2 | ForEach-Object { Write-Host "  $_" }
  New-Item -ItemType Directory -Force $logs | Out-Null; Copy-Item $TesteLog (Join-Path $logs 'hydrothunder_999.log')
} else { Read-Host 'Aperte Enter DEPOIS de fechar o jogo' }
[IO.File]::WriteAllText($toml, $original)
Remove-Item $backup -ErrorAction SilentlyContinue
$log = Get-ChildItem $logs -Filter '*.log' | Where-Object { $known -notcontains $_.Name } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (!$log) { Write-Host 'Nao achei um log novo. O jogo foi aberto?' -ForegroundColor Red; Read-Host 'Enter para sair'; exit 1 }
$lines = Get-Content $log.FullName
$inv = [Globalization.CultureInfo]::InvariantCulture
function Avg($pattern, $key, $last) {
  $v = @($lines | Where-Object { $_ -match $pattern } | Select-Object -Last $last | ForEach-Object {
      $m = [regex]::Match($_, "[ (]$key=([\d.]+)"); if ($m.Success) { [double]::Parse($m.Groups[1].Value, $inv) } })
  if ($v.Count) { [math]::Round(($v | Measure-Object -Average).Average, 2) } else { 'n/a' }
}
# The race is the end of the session: average the last ~30 s (15 reports of 2 s).
$gpu = ($lines | Where-Object { $_ -match '\* 0: ' } | Select-Object -First 1) -replace '^.*\* 0: ', ''
$summary = @(
  "Placa: $gpu"
  "Log: $($log.Name)"
  "FPS medio (ultimos ~30 s): $(Avg '\[ht-perf\] t=' 'fps' 15)"
  "GPU ms por quadro: $(Avg '\[ht-perf\] t=' 'total' 15)  (resolve $(Avg '\[ht-perf\] t=' 'resolve' 15), transfers $(Avg '\[ht-perf\] t=' 'transfers' 15))"
  "CPU (thread de GPU) ms por quadro: $(Avg '\[ht-cpu\]' 'busy' 15)"
  "Pior quadro ms: $(Avg '\[ht-perf\] t=' 'max_frame' 15)"
)
$out = if ($Saida) { $Saida } else { Join-Path ([Environment]::GetFolderPath('Desktop')) 'hydro-medicao' }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item $log.FullName (Join-Path $out $log.Name)
Copy-Item $toml (Join-Path $out 'hydrothunder.toml')
$summary | Set-Content (Join-Path $out 'resumo.txt') -Encoding UTF8
Write-Host ''
$summary | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host "Pronto. Configuracao restaurada. Mande a pasta $out para o Claude." -ForegroundColor Green
if (!$TesteLog) { Read-Host 'Enter para sair' }
