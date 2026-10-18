# GPU cost of one configuration, measured inside a race (Windows + NVIDIA).
# Isolated copy outputs/_teste_modern/game-test, virtual pad (HT_BOOT_TEST),
# throttle held with steering taps. Logs board power/clocks/utilization with
# nvidia-smi and the game's own [ht-present] FPS, over the race only.
# Usage: gpu_race.ps1 -Name base -Set 'fps_limit=60;resolution_scale=1'
param([Parameter(Mandatory)][string]$Name,[string]$Set='',[string]$Cvars='',[string]$OutRoot='',
      [int]$MenuSeconds=75,[string]$StillShots='',[int]$RaceSeconds=55,[int]$SkipSeconds=20)
$ErrorActionPreference='Stop'
$inv=[Globalization.CultureInfo]::InvariantCulture
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$workspace=Split-Path (Split-Path $project -Parent) -Parent
$sandbox=Join-Path $workspace 'outputs/_teste_modern/game-test'
if(!$OutRoot){$OutRoot=Join-Path $workspace 'outputs/_teste_gpu'}
$out=Join-Path $OutRoot $Name
New-Item -ItemType Directory -Force $out | Out-Null
$settings=Join-Path $OutRoot 'settings'   # frame generation stays off here
New-Item -ItemType Directory -Force $settings | Out-Null
@('HT-LS-2','0','2','0','0') | Set-Content -LiteralPath (Join-Path $settings 'lossless-scaling.txt') -Encoding ASCII
function Keep($p){}
function Shot([string]$n){}
$script:unfocused=0
function Pad([int]$b,[int]$rt,[int]$lx){[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),"$b $rt 0 0 $lx 0 0 0")}

# Same baseline for every run (Ian's screen: 1080p borderless), plus overrides.
$values=[ordered]@{fps_limit='60';video_mode_refresh_rate='60.0';vsync='true';ht_import_on_start='false';
  ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';window_width='1920';window_height='1080';
  ht_output_width='1920';resolution_scale='1';ht_render_quality='1';present_effect='"fsr"';ht_perf_log='true';ht_show_fps='true';log_level='"warn"'}
foreach($pair in ($Set -split ';' | Where-Object {$_})){$k,$v=$pair -split '=',2;$values[$k.Trim()]=$v.Trim()}
$config=Join-Path $sandbox 'hydrothunder.toml'
$original=[System.IO.File]::ReadAllText($config); [System.IO.File]::WriteAllText((Join-Path $out 'config-before.toml'),$original)
$text=$original
foreach($key in $values.Keys){$line="$key = $($values[$key])";if($text -match "(?m)^$key\s*="){$text=[regex]::Replace($text,"(?m)^$key\s*=.*$",$line)}else{$text=$line+"`n"+$text}}
[System.IO.File]::WriteAllText($config,$text)
($values.GetEnumerator() | ForEach-Object {"$($_.Key) = $($_.Value)"}) | Set-Content (Join-Path $out 'config.txt')
$logDir=Join-Path $sandbox 'logs'
$known=@(Get-ChildItem $logDir -Filter '*.log' -ErrorAction SilentlyContinue | ForEach-Object Name)
Pad 0 0 0
$env:HT_BOOT_TEST='1';$env:HT_SETTINGS_ROOT=$settings;$env:HT_TEST_CVARS=$Cvars;if($Cvars){"HT_TEST_CVARS=$Cvars" | Add-Content (Join-Path $out 'config.txt')}
$smiFile=Join-Path $out 'nvidia-smi.csv'
$smi=$null
try{
  $p=Start-Process -FilePath (Join-Path $sandbox 'hydrothunder.exe') -WorkingDirectory $sandbox -ArgumentList '--ht_direct_launch=true' -PassThru
  Start-Sleep 15
  # Start only early (title screens); later a stray Start would pause the race,
  # while A also resumes from the pause menu ("Retomar Jogo" is preselected).
  $end=(Get-Date).AddSeconds($MenuSeconds);$i=0
  while((Get-Date) -lt $end -and !$p.HasExited){Pad $(if($i%4 -eq 3 -and $i -lt 28){16}else{4096}) 0 0;Start-Sleep -Milliseconds 150;Pad 0 0 0;Start-Sleep -Milliseconds 1100;$i++;Keep $p}
  # Image comparison: no input after the menus, lossless shots of the same
  # standing scene at fixed delays, then quit without racing.
  if($StillShots){$t0=Get-Date;foreach($s in ($StillShots -split ',')){while(((Get-Date)-$t0).TotalSeconds -lt [double]$s){Start-Sleep -Milliseconds 50};Keep $p;Shot ('still-'+$s)};$crashed=$p.HasExited;return}
  $raceStart=Get-Date; $raceStart.ToString('o') | Set-Content (Join-Path $out 'race-start.txt'); $cpuRows=@(); $cpuPrev=$null; $cpuTime=$null; $threadStart=@{}; $threadRows=@(); $threadWall=$null
  $smi=Start-Process nvidia-smi -ArgumentList '--query-gpu=timestamp,power.draw,clocks.gr,clocks.mem,utilization.gpu,temperature.gpu,pstate --format=csv,noheader,nounits -lms 500' -RedirectStandardOutput $smiFile -WindowStyle Hidden -PassThru
  $end=$raceStart.AddSeconds($RaceSeconds);$t=0
  while((Get-Date) -lt $end -and !$p.HasExited){
    $p.Refresh(); $now=Get-Date; $cpuNow=$p.TotalProcessorTime.TotalMilliseconds; if($cpuPrev -ne $null){$cpuRows += [pscustomobject]@{Elapsed=($now-$raceStart).TotalSeconds;CpuMsPerSec=($cpuNow-$cpuPrev)/($now-$cpuTime).TotalSeconds}}; $cpuPrev=$cpuNow; $cpuTime=$now; if($t -eq 50){foreach($th in $p.Threads){try{$threadStart[$th.Id]=$th.TotalProcessorTime.TotalMilliseconds}catch{}}; $threadWall=Get-Date}; if($t -eq 125 -and $threadWall){$tw=((Get-Date)-$threadWall).TotalSeconds;foreach($th in $p.Threads){try{if($threadStart.ContainsKey($th.Id)){$threadRows += [pscustomobject]@{Thread=$th.Id;CpuMsPerSec=($th.TotalProcessorTime.TotalMilliseconds-$threadStart[$th.Id])/$tw}}}catch{}}; $threadRows | Sort-Object CpuMsPerSec -Descending | Export-Csv (Join-Path $out 'cpu-threads.csv') -NoTypeInformation}; $lx=switch($t%6){1{-20000}4{20000}default{0}}
    Pad 0 255 $lx;Start-Sleep -Milliseconds 400;$t++;if($t%5 -eq 0){Keep $p}
    if($t -eq 50 -or $t -eq 130){Shot "race-$t"}
  }
  $crashed=$p.HasExited
}finally{
  if($smi -and !$smi.HasExited){Stop-Process -Id $smi.Id -Force}
  Pad 0 0 0
  if($p -and !$p.HasExited){Stop-Process -Id $p.Id -Force}
  [System.IO.File]::WriteAllText($config,$original); [System.IO.File]::WriteAllText((Join-Path $out 'config-after.toml'),[System.IO.File]::ReadAllText($config))
}
Start-Sleep 2
$log=Get-ChildItem $logDir -Filter '*.log' | Where-Object {$known -notcontains $_.Name} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if(!$log){$log=Get-ChildItem $logDir -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1}
Copy-Item $log.FullName (Join-Path $out 'game.log') -Force
$from=$raceStart.AddSeconds($SkipSeconds)
$fromText=$from.ToString('HH:mm:ss')
$lines=Get-Content $log.FullName
function InRace($line){$m=[regex]::Match($line,'(\d\d:\d\d:\d\d)');$m.Success -and $m.Groups[1].Value -ge $fromText -and $m.Groups[1].Value -le $raceStart.AddSeconds(55).ToString('HH:mm:ss')}
$fps=@($lines | Where-Object {$_ -match 'presented fps=' -and (InRace $_)} | ForEach-Object {[double]::Parse([regex]::Match($_,'presented fps=([\d.]+)').Groups[1].Value,$inv)})
$worst=@($lines | Where-Object {$_ -match 'presented fps=' -and (InRace $_)} | ForEach-Object {[double]::Parse([regex]::Match($_,'worst=([\d.]+)').Groups[1].Value,$inv)})
$busy=@($lines | Where-Object {$_ -match 'guest gpu busy' -and (InRace $_)} | ForEach-Object {[int]([regex]::Match($_,'guest gpu busy (\d+)%').Groups[1].Value)})
$rows=@(Get-Content $smiFile | ForEach-Object {$c=$_ -split ',\s*';if($c.Count -ge 6){try{[pscustomobject]@{t=[datetime]::ParseExact($c[0].Trim(),'yyyy/MM/dd HH:mm:ss.fff',$inv);w=[double]::Parse($c[1],$inv);clk=[double]$c[2];mem=[double]$c[3];util=[double]$c[4];temp=[double]$c[5]}}catch{}}} | Where-Object {$_.t -ge $from -and $_.t -le $raceStart.AddSeconds(55)})
function Avg($xs){if(@($xs).Count){[math]::Round((@($xs) | Measure-Object -Average).Average,1)}else{0}}
$cpuRows | Export-Csv (Join-Path $out 'cpu.csv') -NoTypeInformation; $result=[pscustomobject]@{CPUmsPorSegundo=Avg @($cpuRows | Where-Object {$_.Elapsed -ge $SkipSeconds -and $_.Elapsed -le 55} | ForEach-Object {$_.CpuMsPerSec});
  Teste=$Name;Ajustes="$Set $Cvars".Trim();FPS=Avg $fps;FPSmin=if($fps){($fps|Measure-Object -Minimum).Minimum}else{0};PiorQuadro_ms=if($worst){($worst|Measure-Object -Maximum).Maximum}else{0}
  Watts=Avg $rows.w;ClockGPU=Avg $rows.clk;ClockMem=Avg $rows.mem;UsoGPU=Avg $rows.util;GPUjogo=Avg $busy;Temp=Avg $rows.temp;Amostras=$rows.Count;TravouOuFechou=[bool]$crashed;SemFoco=$null;Log=$log.Name
}
$result | ConvertTo-Json | Set-Content (Join-Path $out 'result.json') -Encoding UTF8
$result
