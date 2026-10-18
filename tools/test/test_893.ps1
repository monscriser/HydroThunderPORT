param([ValidateSet('proof','positive','rtt-proof','rtt-positive','stable','ab','cap-ab')][string]$Mode='proof',[ValidateSet(1,2)][int]$Scale=1)
$ErrorActionPreference='Stop'
if(Test-Path (Join-Path $PSScriptRoot 'PAUSAR-TESTES.txt')){throw 'Testes pausados para passagem a outro agente; leia a retomada antes de continuar.'}
$project='C:\Users\Monscriser\Documents\Codex\2026-10-13\usan\work\Hydro-Thunder-fontes-revisao-7'
$game='C:\Users\Monscriser\Documents\Codex\2026-10-13\usan\outputs\_teste_modern\game-test'
$task=Split-Path $PSScriptRoot -Parent
$out=Join-Path $task 'outputs\validacao-8.9.3'
$name="$Mode-scale$Scale"
if(Get-Process hydrothunder -ErrorAction SilentlyContinue){throw 'Jogo ja aberto; nao interromper.'}
if(Test-Path (Join-Path $out $name)){throw "Resultado ja existe: $name"}
$backup=Join-Path $game ("_to_delete\antes-893-$name-"+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$bins=@('hydrothunder.exe','librexruntime.dll','rexgpu-xenos.dll')
$config=Join-Path $game 'hydrothunder.toml'
foreach($f in $bins){Copy-Item -LiteralPath (Join-Path $game $f) -Destination $backup}
Copy-Item -LiteralPath $config -Destination $backup
try {
  foreach($f in $bins){Copy-Item -LiteralPath (Join-Path $PSScriptRoot "final-windows\$f") -Destination (Join-Path $game $f) -Force}
  New-Item -ItemType Directory -Path $out -Force | Out-Null
  Get-FileHash ($bins | ForEach-Object {Join-Path $game $_}) | ConvertTo-Json | Set-Content (Join-Path $out "$name-binaries.json")
  $flags='ht_zpd_batch=true;ht_dynamic_constants=true'
  if($Mode -in 'proof','positive','rtt-proof','rtt-positive') {
    $flags+=';ht_zpd_batch_verify=true;ht_dynamic_constants_verify=true;ht_float_constant_runs_verify=true;ht_texture_direct_verify=true;ht_direct_resolve_verify=true;ht_resolve_texture_verify=true;ht_request_range_fast_verify=true;vulkan_validation_enabled=true'
    if($Mode -like 'rtt-*'){$flags=$flags.Replace('ht_direct_resolve_verify=true','ht_direct_resolve_verify=false')}
    if($Mode -like '*positive'){$flags+=';ht_zpd_batch_verify_break=true;ht_dynamic_constants_verify_break=true;ht_float_constant_runs_verify_break=true;ht_direct_resolve_verify_break=true;ht_resolve_texture_verify_break=true;ht_request_range_fast_verify_break=true'}
    if($Mode -eq 'rtt-positive'){$flags=$flags.Replace('ht_direct_resolve_verify_break=true','ht_direct_resolve_verify_break=false')}
  }
  if($Mode -in 'ab','cap-ab') {
    $cap=if($Mode -eq 'cap-ab'){'60'}else{'0'}
    $shots=if($Mode -eq 'cap-ab'){''}else{'20,45,70'}
    foreach($state in 'false','true') {
      & "$project\tools\test\gpu_race_batch.ps1" -Name "$name-$state" -Set "fps_limit=$cap;resolution_scale=$Scale" -Cvars "ht_zpd_batch=$state;ht_dynamic_constants=$state" -OutRoot $out -RaceSeconds 35 -SkipSeconds 10 -RaceShots $shots
    }
  } else {
    & "$project\tools\test\gpu_race_batch.ps1" -Name $name -Set "fps_limit=60;resolution_scale=$Scale" -Cvars $flags -OutRoot $out -RaceSeconds 35 -SkipSeconds 10 -RaceShots '20,45,70'
  }
} finally {
  if(Get-Process hydrothunder -ErrorAction SilentlyContinue){throw 'Jogo ainda aberto; fechar antes de restaurar backup.'}
  foreach($f in $bins){Copy-Item -LiteralPath (Join-Path $backup $f) -Destination (Join-Path $game $f) -Force}
  if((Get-FileHash $config).Hash -ne (Get-FileHash (Join-Path $backup 'hydrothunder.toml')).Hash){Copy-Item -LiteralPath $config -Destination (Join-Path $backup 'config-after.toml');Copy-Item -LiteralPath (Join-Path $backup 'hydrothunder.toml') -Destination $config -Force}
  Get-FileHash (($bins+'hydrothunder.toml') | ForEach-Object {Join-Path $game $_}) | ConvertTo-Json | Set-Content (Join-Path $out "$name-restored.json")
}
