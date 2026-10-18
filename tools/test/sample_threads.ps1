# Sampling profile of the busiest game threads (measurement only). Opens the isolated test
# copy like gpu_menu.ps1, waits, finds the top CPU threads, samples each one (RIP + return
# address candidates from the stack) and writes module+offset histograms to -Out.
# Offsets are symbolized afterwards in WSL (tools/test/symbolize_samples.sh).
param([string]$Set='resolution_scale=2;ht_render_quality=0',[string]$Cvars='',[int]$Warmup=35,
      [int]$Top=4,[int]$Samples=600,[int]$Depth=6,[Parameter(Mandatory)][string]$Out)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
$settings=Join-Path $workspace 'outputs/_teste_gpu/settings'
New-Item -ItemType Directory -Force $settings,$Out | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot 'HtSampler.cs')
$values=[ordered]@{fps_limit='60';ht_import_on_start='false';ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';
  window_width='1920';window_height='1080';ht_output_width='1920';resolution_scale='1';ht_render_quality='1';present_effect='"fsr"';ht_perf_log='true';log_level='"warn"'}
foreach($pair in ($Set -split ';' | Where-Object {$_})){$k,$v=$pair -split '=',2;$values[$k.Trim()]=$v.Trim()}
$config=Join-Path $sandbox 'hydrothunder.toml'
$original=[System.IO.File]::ReadAllText($config);$text=$original
foreach($key in $values.Keys){$line="$key = $($values[$key])";if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}}
[System.IO.File]::WriteAllText($config,$text)
[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),'0 0 0 0 0 0 0 0')
$env:HT_BOOT_TEST='1';$env:HT_SETTINGS_ROOT=$settings;$env:HT_TEST_CVARS=$Cvars
try{
  $p=Start-Process (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -PassThru
  Start-Sleep $Warmup
  $p.Refresh()
  $mods=@($p.Modules | ForEach-Object {[pscustomobject]@{Name=$_.ModuleName;Base=$_.BaseAddress.ToInt64();End=$_.BaseAddress.ToInt64()+$_.ModuleMemorySize}})
  $t0=@{};foreach($th in $p.Threads){try{$t0[$th.Id]=$th.TotalProcessorTime.TotalMilliseconds}catch{}}
  Start-Sleep 5;$p.Refresh()
  $busy=@($p.Threads | ForEach-Object {try{[pscustomobject]@{Id=$_.Id;Ms=$_.TotalProcessorTime.TotalMilliseconds-$t0[$_.Id]}}catch{}} | Sort-Object Ms -Descending | Select-Object -First $Top)
  $mb=[long[]]@($mods.Base);$me=[long[]]@($mods.End)
  foreach($b in $busy){
    $res=[HtSampler]::Sample($p.Id,$b.Id,$Samples,2,$Depth,$mb,$me)
    $lines=foreach($s in $res){ ($s | ForEach-Object { $a=$_; if(!$a){''}else{ $m=$mods | Where-Object {$a -ge $_.Base -and $a -lt $_.End} | Select-Object -First 1; if($m){'{0}+0x{1:X}' -f $m.Name,($a-$m.Base)}else{'0x{0:X}' -f $a} } }) -join ' ' }
    $lines | Set-Content (Join-Path $Out ("thread-{0}-cpu{1}ms.txt" -f $b.Id,[math]::Round($b.Ms/5)))
    "thread $($b.Id): $([math]::Round($b.Ms/5)) ms CPU/s, $($res.Count) amostras"
  }
}
finally{if($p -and !$p.HasExited){$p.CloseMainWindow()|Out-Null;Start-Sleep 4;if(!$p.HasExited){Stop-Process -Id $p.Id -Force}};[System.IO.File]::WriteAllText($config,$original)}
