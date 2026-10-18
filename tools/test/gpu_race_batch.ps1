# GPU cost of one configuration, measured inside a race (Windows + NVIDIA).
# Isolated copy outputs/_teste_modern/game-test, virtual pad (HT_BOOT_TEST),
# throttle held with steering taps. Logs board power/clocks/utilization with
# nvidia-smi and the game's own [ht-present] FPS, over the race only.
# Usage: gpu_race.ps1 -Name base -Set 'fps_limit=60;resolution_scale=1'
param([Parameter(Mandatory)][string]$Name,[string]$Set='',[string]$Cvars='',[string]$OutRoot='',
      [int]$MenuSeconds=75,[string]$StillShots='',[string]$RaceShots='50,130',[int]$RaceSeconds=55,[int]$SkipSeconds=20,[string]$DrivePlan='normal')
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
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
# The game pauses when it loses focus: keep it in front and record if it was not.
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class GrWin{[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);[DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")]public static extern int GetWindowThreadProcessId(IntPtr h,out int pid);
public static bool Front(int pid){int p;GetWindowThreadProcessId(GetForegroundWindow(),out p);return p==pid;}}
'@
$script:unfocused=0
function Keep($p){if(![GrWin]::Front($p.Id)){$script:unfocused++;$p.Refresh();[void][GrWin]::SetForegroundWindow($p.MainWindowHandle)}}
function Shot([string]$n){$b=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds;$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height;$g=[System.Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size);$bmp.Save((Join-Path $out "$n.png"),[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$bmp.Dispose()}
function Pad([int]$b,[int]$rt,[int]$lx){[System.IO.File]::WriteAllText((Join-Path $sandbox 'ht_test_input.txt'),"$b $rt 0 0 $lx 0 0 0")}

# Same baseline for every run (Ian's screen: 1080p borderless), plus overrides.
$values=[ordered]@{fps_limit='60';video_mode_refresh_rate='60.0';vsync='true';ht_import_on_start='false';
  ht_game_fullscreen='true';fullscreen='true';fullscreen_mode='"borderless"';window_width='1920';window_height='1080';
  ht_output_width='1920';resolution_scale='1';ht_render_quality='1';present_effect='"fsr"';ht_perf_log='true';ht_show_fps='true';log_level='"warn"'}
foreach($pair in ($Set -split ';' | Where-Object {$_})){$k,$v=$pair -split '=',2;$values[$k.Trim()]=$v.Trim()}
$config=Join-Path $sandbox 'hydrothunder.toml'
$original=[System.IO.File]::ReadAllText($config)
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
  while((Get-Date) -lt $end -and !$p.HasExited){Pad $(if($i -eq 3){16}else{4096}) 0 0;Start-Sleep -Milliseconds 150;Pad 0 0 0;Start-Sleep -Milliseconds 1100;$i++;Keep $p
    # Stop confirming as soon as the actual race scene is rendered.
    $current=Get-ChildItem $logDir -Filter '*.log' | Where-Object {$known -notcontains $_.Name} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if($current){
      $raceLine=Get-Content -LiteralPath $current.FullName -Tail $(if($Cvars -match 'ht_zpd_batch_trace=true'){20000}else{80}) | Where-Object {$_ -match '\[ht-perf\].* per frame: draws=(\d+).* zpd=([\d.]+)' -and [int]$Matches[1] -gt 1200 -and [double]::Parse($Matches[2],$inv) -gt 10} | Select-Object -Last 1
      if($raceLine){$raceLine | Set-Content (Join-Path $out 'scene-detected.txt');break}
    }
  }
  # A resumes if a title-screen Start was delayed into the race pause menu.
  if($DrivePlan -notlike 'natural-clean*'){Pad 4096 0 0;Start-Sleep -Milliseconds 150;Pad 0 0 0;Start-Sleep -Milliseconds 300}
  if(!$StillShots -and $DrivePlan -notlike 'natural-clean*'){Start-Sleep 5} # let the countdown finish before the measured input
  # Back/Select recovers the boat if the last menu confirmation already boosted it.
  if(!$StillShots -and $DrivePlan -notlike 'natural*' -and $DrivePlan -notlike 'replay:*'){Pad 32 0 0;Start-Sleep -Milliseconds 200;Pad 0 0 0;Start-Sleep 1}
  # Image comparison: no input after the menus, lossless shots of the same
  # standing scene at fixed delays, then quit without racing.
  if($StillShots){$t0=Get-Date;foreach($s in ($StillShots -split ',')){while(((Get-Date)-$t0).TotalSeconds -lt [double]$s){Start-Sleep -Milliseconds 50};Keep $p;Shot ('still-'+$s)};$crashed=$p.HasExited;$standingLog=Get-ChildItem $logDir -Filter '*.log' | Where-Object {$known -notcontains $_.Name} | Sort-Object LastWriteTime -Descending | Select-Object -First 1;if($standingLog){Copy-Item -LiteralPath $standingLog.FullName -Destination (Join-Path $out 'game.log')};return}
  if($DrivePlan -eq 'natural-clean-right'){Start-Sleep 5}
  Shot 'start-before-input'
  $raceStart=Get-Date
  $smi=Start-Process nvidia-smi -ArgumentList '--query-gpu=timestamp,power.draw,clocks.gr,clocks.mem,utilization.gpu,temperature.gpu,pstate --format=csv,noheader,nounits -lms 500' -RedirectStandardOutput $smiFile -WindowStyle Hidden -PassThru
  $end=$raceStart.AddSeconds($RaceSeconds);$t=0
  # 'replay:<csv>': repeats a keyboard drive recorded by gravar_pilotagem.ps1 (ms,mask; bit0 E,
  # 2 Space, 3 X, 4 left, 5 right, 6 Tab). Time 0 = first throttle press. No brake (pad has no LT).
  if($DrivePlan -like 'replay:*'){
    $ev=@(Import-Csv ($DrivePlan.Substring(7)) | ForEach-Object {[pscustomobject]@{ms=[int64]$_.ms;mask=[int]$_.mask}})
    # time 0 = first throttle press held >= 0.5 s (a short tap during the countdown does nothing)
    $first=$null
    for($i=0;$i -lt $ev.Count -and $first -eq $null;$i++){
      if(!($ev[$i].mask -band 1)){continue};$j=$i+1;while($j -lt $ev.Count -and ($ev[$j].mask -band 1)){$j++}
      $upAt=if($j -lt $ev.Count){$ev[$j].ms}else{[int64]::MaxValue};if($upAt-$ev[$i].ms -ge 500){$first=$ev[$i].ms}
    }
    $ev=@($ev | Where-Object {$_.ms -ge $first})
    $shotsLeft=[System.Collections.Generic.List[int]]::new();foreach($s in ($RaceShots -split ',' | Where-Object {$_})){$shotsLeft.Add([int]$s)}
    $sw=[Diagnostics.Stopwatch]::StartNew();$k=0;$lastKeep=0
    while((Get-Date) -lt $end -and !$p.HasExited){
      $now=$sw.ElapsedMilliseconds
      while($k -lt $ev.Count -and ($ev[$k].ms-$first) -le $now){
        $m=$ev[$k].mask;$b=0;if($m -band 4){$b+=4096};if($m -band 8){$b+=16384};if($m -band 64){$b+=32}
        $lx=0;if($m -band 16){$lx-=32768};if($m -band 32){$lx+=32767}
        Pad $b $(if($m -band 1){255}else{0}) $lx;$k++
      }
      if($now-$lastKeep -gt 2000){Keep $p;$lastKeep=$now}
      if($shotsLeft.Count -and $now -ge $shotsLeft[0]*400){Shot ("race-"+$shotsLeft[0]);$shotsLeft.RemoveAt(0)}
      Start-Sleep -Milliseconds 5
    }
    $end=Get-Date
  }
  while((Get-Date) -lt $end -and !$p.HasExited){
    $lx=if($DrivePlan -eq 'natural-clean-right'){if($t -eq 0){20000}else{0}}elseif($DrivePlan -like 'natural*'){if($t -lt 20){0}else{switch($t%10){2{-18000}7{18000}default{0}}}}elseif($DrivePlan -eq 'turn-right-short'){if($t -lt 3){32767}else{0}}elseif($DrivePlan -eq 'turn-right'){if($t -lt 10){32767}else{0}}elseif($DrivePlan -eq 'turn-left'){if($t -lt 3){-32768}else{0}}elseif($t -lt 3){8000}else{switch($t%6){1{-8000}4{8000}default{0}}}
    # 'recover': Back (32) every ~4 s puts a stuck boat back on the course ("Aperte Back para recuperar").
    $btn=if($DrivePlan -eq 'recover' -and $t%10 -eq 9 -and $env:HT_NO_BACK -ne '1'){32}else{0}
    # Ian: the boat must go a little to the right at the start, or it runs into the dock.
    if($DrivePlan -eq 'recover'){$lx=if($t -lt $env:HT_RIGHT_TICKS){16000}else{switch($t%10){2{-14000}6{14000}default{0}}}}
    Pad $btn 255 $lx;Start-Sleep -Milliseconds 400;$t++;if($t%5 -eq 0){Keep $p}
    if(($RaceShots -split ',') -contains "$t"){Shot "race-$t"}
  }
  $crashed=$p.HasExited
}finally{
  if($smi -and !$smi.HasExited){Stop-Process -Id $smi.Id -Force}
  Pad 0 0 0
  if($p -and !$p.HasExited){$p.CloseMainWindow()|Out-Null;Start-Sleep 5;if(!$p.HasExited){Stop-Process -Id $p.Id -Force}}
  [System.IO.File]::WriteAllText($config,$original)
}
Start-Sleep 2
$log=Get-ChildItem $logDir -Filter '*.log' | Where-Object {$known -notcontains $_.Name} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if(!$log){$log=Get-ChildItem $logDir -Filter '*.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1}
Copy-Item $log.FullName (Join-Path $out 'game.log') -Force
$from=$raceStart.AddSeconds($SkipSeconds)
$fromText=$from.ToString('HH:mm:ss')
$lines=Get-Content $log.FullName
function InRace($line){$m=[regex]::Match($line,'(\d\d:\d\d:\d\d)');$m.Success -and $m.Groups[1].Value -ge $fromText}
$fps=@($lines | Where-Object {$_ -match 'presented fps=' -and (InRace $_)} | ForEach-Object {[double]::Parse([regex]::Match($_,'presented fps=([\d.]+)').Groups[1].Value,$inv)})
$worst=@($lines | Where-Object {$_ -match 'presented fps=' -and (InRace $_)} | ForEach-Object {[double]::Parse([regex]::Match($_,'worst=([\d.]+)').Groups[1].Value,$inv)})
$busy=@($lines | Where-Object {$_ -match 'guest gpu busy' -and (InRace $_)} | ForEach-Object {[int]([regex]::Match($_,'guest gpu busy (\d+)%').Groups[1].Value)})
$rows=@(Get-Content $smiFile | ForEach-Object {$c=$_ -split ',\s*';if($c.Count -ge 6){try{[pscustomobject]@{t=[datetime]::ParseExact($c[0].Trim(),'yyyy/MM/dd HH:mm:ss.fff',$inv);w=[double]::Parse($c[1],$inv);clk=[double]$c[2];mem=[double]$c[3];util=[double]$c[4];temp=[double]$c[5]}}catch{}}} | Where-Object {$_.t -ge $from})
function Avg($xs){if(@($xs).Count){[math]::Round((@($xs) | Measure-Object -Average).Average,1)}else{0}}
$result=[pscustomobject]@{
  Teste=$Name;Ajustes="$Set $Cvars".Trim();FPS=Avg $fps;FPSmin=if($fps){($fps|Measure-Object -Minimum).Minimum}else{0};PiorQuadro_ms=if($worst){($worst|Measure-Object -Maximum).Maximum}else{0}
  Watts=Avg $rows.w;ClockGPU=Avg $rows.clk;ClockMem=Avg $rows.mem;UsoGPU=Avg $rows.util;GPUjogo=Avg $busy;Temp=Avg $rows.temp;Amostras=$rows.Count;TravouOuFechou=[bool]$crashed;SemFoco=$script:unfocused;Log=$log.Name
}
$result | ConvertTo-Json | Set-Content (Join-Path $out 'result.json') -Encoding UTF8
$result
