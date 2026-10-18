param([int]$Limit=60)
$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
$config=Join-Path $sandbox 'hydrothunder.toml'
$text=Get-Content -LiteralPath $config -Raw
$values=@{fps_limit="$Limit";video_mode_refresh_rate=$(if($Limit -eq 0){'60.0'}else{"$Limit.0"});vsync='true';ht_import_on_start='false';ht_game_fullscreen='false';fullscreen='false';window_width='1280';window_height='720';ht_output_width='1280';resolution_scale='1';present_effect='"fsr"';ht_perf_log='true';ht_show_fps='true';log_level='"warn"'}
foreach($key in $values.Keys){$line="$key = $($values[$key])";if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}}
[System.IO.File]::WriteAllText($config,$text)
[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),'0 0 0 0 0 0 0 0')
$oldTest=$env:HT_BOOT_TEST;$oldSettings=$env:HT_SETTINGS_ROOT
try{
  $env:HT_BOOT_TEST='1';$env:HT_SETTINGS_ROOT=Join-Path $workspace 'outputs/_teste_modern/game-test-settings'
  $process=Start-Process -FilePath (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -WindowStyle Normal -PassThru
  "test_pid=$($process.Id) fps_limit=$Limit settings=$env:HT_SETTINGS_ROOT"
}finally{$env:HT_BOOT_TEST=$oldTest;$env:HT_SETTINGS_ROOT=$oldSettings}
