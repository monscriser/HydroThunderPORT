# ht_request_range_fast proofs + A/B (measurement build with the [ht-reqrange] timer).
# Swaps the candidate rexgpu-xenos.dll into game-test and ALWAYS restores the tested DLL.
param([Parameter(Mandatory)][string]$Dll,[Parameter(Mandatory)][string]$Out)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$g=Join-Path $workspace 'outputs/_teste_modern/game-test'
New-Item -ItemType Directory -Force $Out | Out-Null
$keep=Join-Path $g '_to_delete/rexgpu-xenos-8.8-testada.dll'
if(!(Test-Path $keep)){Copy-Item (Join-Path $g 'rexgpu-xenos.dll') $keep}
$tested=(Get-FileHash $keep).Hash
function NewestLog{Get-ChildItem (Join-Path $g 'logs') -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1}
try{
  Copy-Item $Dll (Join-Path $g 'rexgpu-xenos.dll') -Force
  foreach($run in @(@{n='menu-verify';c='ht_request_range_fast_verify=true'},@{n='menu-verify-break';c='ht_request_range_fast_verify=true;ht_request_range_fast_verify_break=true'})){
    & (Join-Path $PSScriptRoot 'gpu_menu.ps1') -Set '' -Cvars $run.c -Seconds 40 | Out-Null
    $l=NewestLog; Copy-Item $l.FullName (Join-Path $Out "$($run.n).log")
    "$($run.n): " + ((Select-String -Path $l.FullName -Pattern 'ht-rrfast-verify' | Select-Object -Last 1).Line)
  }
  foreach($run in @(@{n='corrida-fast-off';c='ht_request_range_fast=false'},@{n='corrida-fast-on';c=''})){
    $r=& (Join-Path $PSScriptRoot 'gpu_race.ps1') -Name $run.n -OutRoot $Out -Cvars $run.c
    "$($run.n): FPS=$($r.FPS) pior=$($r.PiorQuadro_ms) semfoco=$($r.SemFoco) travou=$($r.TravouOuFechou)"
  }
}finally{
  Copy-Item $keep (Join-Path $g 'rexgpu-xenos.dll') -Force
  "DLL testada restaurada: " + ((Get-FileHash (Join-Path $g 'rexgpu-xenos.dll')).Hash -eq $tested)
}
