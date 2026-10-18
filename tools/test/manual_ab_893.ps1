param([switch]$SemJogo,[string]$Jogo='')
$ErrorActionPreference='Stop'
$kit=$PSScriptRoot
$game=if($Jogo){$Jogo}else{'C:\Users\Monscriser\Documents\Codex\2026-10-13\usan\outputs\_teste_modern\game-test'}
$exe=Join-Path $game 'hydrothunder.exe'
$dll=Join-Path $game 'rexgpu-xenos.dll'
$candidate=Join-Path $kit 'rexgpu-xenos-candidata.dll'
$candidateExe=Join-Path $kit 'hydrothunder-candidato.exe'
$candidateRuntime=Join-Path $kit 'librexruntime-candidato.dll'
$runtime=Join-Path $game 'librexruntime.dll'
if(Get-Process hydrothunder -ErrorAction SilentlyContinue){throw 'Feche o jogo antes de iniciar este kit.'}
foreach($f in $candidateExe,$candidateRuntime,$runtime){if(!(Test-Path -LiteralPath $f)){throw "Arquivo ausente: $f"}}
foreach($f in $exe,$dll,$candidate,(Join-Path $game 'hydrothunder.toml')){if(!(Test-Path -LiteralPath $f)){throw "Arquivo ausente: $f"}}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$backup=Join-Path $game "_to_delete\antes-medicao-manual-$stamp"
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $dll -Destination $backup
Copy-Item -LiteralPath $exe,$runtime -Destination $backup
Get-FileHash $dll,$candidate | ConvertTo-Json | Set-Content (Join-Path $backup 'hashes.json')
$run=Join-Path $kit "resultados-$stamp"
New-Item -ItemType Directory -Path $run | Out-Null
$env:HT_BOOT_TEST='';$env:HT_TEST_CVARS='';$env:HT_TRACE_FRAME=''
try {
  Copy-Item -LiteralPath $candidate -Destination $dll -Force
  Copy-Item -LiteralPath $candidateExe -Destination $exe -Force
  Copy-Item -LiteralPath $candidateRuntime -Destination $runtime -Force
  foreach($label in 'A','B') {
    $enabled=if($label -eq 'A'){'false'}else{'true'}
    $settings=Join-Path $run "config-$label"
    New-Item -ItemType Directory -Path $settings | Out-Null
    $text=[IO.File]::ReadAllText((Join-Path $game 'hydrothunder.toml'))
    $values=[ordered]@{fps_limit='0';vsync='true';video_mode_refresh_rate='60.0';ht_perf_log='true';ht_show_fps='true';ht_import_on_start='false';
      resolution_scale='1';ht_render_quality='1';present_effect='"fsr"';ht_output_width='1920';window_width='1920';window_height='1080';
      ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';mnk_mode='true';log_level='"warn"';
      ht_zpd_batch=$enabled;ht_dynamic_constants=$enabled;ht_zpd_batch_verify='false';ht_zpd_batch_verify_break='false';
      ht_dynamic_constants_verify='false';ht_dynamic_constants_verify_break='false';vulkan_validation_enabled='false'}
    foreach($key in $values.Keys){
      $line="$key = $($values[$key])"
      if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}
    }
    [IO.File]::WriteAllText((Join-Path $settings 'hydrothunder.toml'),$text)
    $quoted='"'+$game.Replace('\','\\')+'"'
    [IO.File]::WriteAllText((Join-Path $settings 'library-location.txt'),"HTH-LIBRARY-1`n$quoted`n")
    @('HT-LS-2','0','2','0','0') | Set-Content (Join-Path $settings 'lossless-scaling.txt') -Encoding ASCII
    $env:HT_SETTINGS_ROOT=$settings
    Write-Host "`n=== Rodada $label ===" -ForegroundColor Cyan
    Write-Host 'Jogue Asgard com o mesmo barco e dirija por pelo menos 60 segundos.'
    Write-Host 'No fim, feche a janela do jogo. Depois sera aberta a outra rodada.'
    if(!$SemJogo){
      Read-Host 'Pressione Enter quando estiver pronto'
      $p=Start-Process -FilePath $exe -WorkingDirectory $game -ArgumentList @('--ht_direct_launch=true',"--ht_zpd_batch=$enabled","--ht_dynamic_constants=$enabled",'--ht_perf_log=true') -PassThru
      $p.WaitForExit()
    }
    Get-FileHash $dll,$exe,$runtime | ConvertTo-Json | Set-Content (Join-Path $settings 'binarios-testados.json')
  }
} finally {
  Copy-Item -LiteralPath (Join-Path $backup 'rexgpu-xenos.dll') -Destination $dll -Force
  Copy-Item -LiteralPath (Join-Path $backup 'hydrothunder.exe') -Destination $exe -Force
  Copy-Item -LiteralPath (Join-Path $backup 'librexruntime.dll') -Destination $runtime -Force
  Get-FileHash $dll,$exe,$runtime | ConvertTo-Json | Set-Content (Join-Path $run 'restaurado.json')
}
Write-Host "`nPronto. Arquivos anteriores restaurados. Resultados em $run" -ForegroundColor Green
if(!$SemJogo){Read-Host 'Pode voltar ao chat e avisar que terminou'}
