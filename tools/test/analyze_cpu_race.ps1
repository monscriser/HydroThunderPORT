param([Parameter(Mandatory)][string]$Folder)
$inv=[Globalization.CultureInfo]::InvariantCulture
$lines=Get-Content -LiteralPath (Join-Path $Folder 'game.log')
$first=$lines | Where-Object {$_ -match 'Player 1 polled buttons=0 RT=255'} | Select-Object -First 1
if(!$first){throw 'Aceleracao nao registrada'}
$accel=[datetime]::ParseExact(([regex]::Match($first,'\[(.*?)\]').Groups[1].Value),'yyyy-MM-dd HH:mm:ss.fff',$inv)
$startFile=Join-Path $Folder 'race-start.txt'
$start=if(Test-Path $startFile){[datetime]::Parse((Get-Content $startFile))}else{$accel}
$from=$start.AddSeconds(20);$to=$start.AddSeconds(55)
$selected=@($lines | Where-Object {if($_ -match '^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d+)\]'){$dt=[datetime]::ParseExact($matches[1],'yyyy-MM-dd HH:mm:ss.fff',$inv);$dt -ge $from -and $dt -le $to}})
function Values($pattern){@($selected | ForEach-Object {if($_ -match $pattern){[double]::Parse($matches[1],$inv)}})}
function Number($v){[double]::Parse(($v -replace ',','.'),$inv)}
function Avg($xs){if(@($xs).Count){[math]::Round(($xs | Measure-Object -Average).Average,3)}else{$null}}
function Max($xs){if(@($xs).Count){($xs | Measure-Object -Maximum).Maximum}else{$null}}
$cpu=@(Import-Csv (Join-Path $Folder 'cpu.csv') | Where-Object {(Number $_.Elapsed) -ge 20 -and (Number $_.Elapsed) -le 55} | ForEach-Object {(Number $_.CpuMsPerSec)})
$r=[pscustomobject]@{Test=(Split-Path $Folder -Leaf);Start=$start.ToString('o');From=$from.ToString('o');To=$to.ToString('o');Fps=Avg (Values 'presented fps=([\d.]+)');WorstPresentMs=Max (Values 'worst=([\d.]+)');MaxFrameMs=Max (Values 'max_frame=([\d.]+)');CpuMsPerSec=Avg $cpu;CpuCores=[math]::Round((Avg $cpu)/1000,4);CpBusyMs=Avg (Values 'busy=([\d.]+) idle');GpuMs=Avg (Values 'total=([\d.]+) transfers');FpsLogSamples=(Values 'presented fps=([\d.]+)').Count;CpuSamples=$cpu.Count;Scope='20-55 seconds after throttle start; visual evidence stored separately'}
$r | ConvertTo-Json | Set-Content (Join-Path $Folder 'measurement.json') -Encoding UTF8
$r