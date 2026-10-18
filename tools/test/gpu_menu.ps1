# Quick GPU check (Ian's suggestion): open the isolated game copy, stay on the
# menu for a few seconds and print the engine's [ht-perf] GPU breakdown.
# Race benchmarks (gpu_race.ps1) remain the proof for final numbers.
param([string]$Set='resolution_scale=2;ht_render_quality=0',[string]$Cvars='',[int]$Seconds=30)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
$settings=Join-Path $workspace 'outputs/_teste_gpu/settings'
New-Item -ItemType Directory -Force $settings | Out-Null
@('HT-LS-2','0','2','0','0') | Set-Content -LiteralPath (Join-Path $settings 'lossless-scaling.txt') -Encoding ASCII
$values=[ordered]@{fps_limit='60';ht_import_on_start='false';ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';
  window_width='1920';window_height='1080';ht_output_width='1920';resolution_scale='1';ht_render_quality='1';present_effect='"fsr"';ht_perf_log='true';log_level='"warn"'}
foreach($pair in ($Set -split ';' | Where-Object {$_})){$k,$v=$pair -split '=',2;$values[$k.Trim()]=$v.Trim()}
$config=Join-Path $sandbox 'hydrothunder.toml'
$original=[System.IO.File]::ReadAllText($config);$text=$original
foreach($key in $values.Keys){$line="$key = $($values[$key])";if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}}
[System.IO.File]::WriteAllText($config,$text)
[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),'0 0 0 0 0 0 0 0')
$logDir=Join-Path $sandbox 'logs';$known=@(Get-ChildItem $logDir -Filter '*.log' | ForEach-Object Name)
$env:HT_BOOT_TEST='1';$env:HT_SETTINGS_ROOT=$settings;$env:HT_TEST_CVARS=$Cvars
try{$p=Start-Process (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -PassThru;Start-Sleep $Seconds}
finally{if($p -and !$p.HasExited){$p.CloseMainWindow()|Out-Null;Start-Sleep 4;if(!$p.HasExited){Stop-Process -Id $p.Id -Force}};[System.IO.File]::WriteAllText($config,$original)}
$log=Get-ChildItem $logDir -Filter '*.log' | Where-Object {$known -notcontains $_.Name} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$perf=@(Get-Content $log.FullName | Where-Object {$_ -match '\[ht-perf\] t='} | Select-Object -Last 5 | Select-Object -SkipLast 1)
$inv=[Globalization.CultureInfo]::InvariantCulture
$row=[ordered]@{Log=$log.Name}
foreach($k in 'fps','total','transfers','resolve','texload','texcopy','upload','texloads'){$v=@($perf | ForEach-Object {$m=[regex]::Match($_,"[ (]$k=([\d.]+)");if($m.Success){[double]::Parse($m.Groups[1].Value,$inv)}});$row[$k]=if($v.Count){[math]::Round(($v|Measure-Object -Average).Average,2)}else{''}}
[pscustomobject]$row
