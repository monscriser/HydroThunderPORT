# Measurement only: runs one short replayed race (game-test, binaries from -BinDir, restored at the end)
# and samples ALL game threads every few ms from process start +FromS for -ForS seconds, to see what
# the game does during the freeze ~10-12 s after the race scene appears. Writes
# <Out>/all-threads.txt ("ms tid hexaddr ...") + modules.txt and the race log. Symbolize with
# tools/test/hitch_analyze.py in WSL. The sampler pauses threads briefly: never a benchmark.
param([Parameter(Mandatory)][string]$Out,[Parameter(Mandatory)][string]$Race,[string]$BinDir,[string]$Cvars='',
      [int]$FromS=16,[int]$ForS=18,[int]$IntervalMs=3,[int]$Depth=14)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot 'HtSamplerAll.cs')
$job=Start-Job -ScriptBlock {param($r,$n,$b,$c)& $r -Name $n -BinDir $b -Cvars $c -Cap 60 -RaceSeconds 30} -ArgumentList $Race,((Split-Path $Out -Leaf)+'-corrida'),$BinDir,$Cvars
try{
  $p=$null;$t=Get-Date
  while(!$p -and ((Get-Date)-$t).TotalSeconds -lt 90){Start-Sleep -Milliseconds 200;$p=Get-Process hydrothunder -ErrorAction SilentlyContinue | Select-Object -First 1}
  if(!$p){throw 'game did not start'}
  $at=$p.StartTime.AddSeconds($FromS)
  while((Get-Date) -lt $at -and !$p.HasExited){Start-Sleep -Milliseconds 50}
  $p.Refresh()
  $mods=@($p.Modules | ForEach-Object {[pscustomobject]@{Name=$_.ModuleName;Base=$_.BaseAddress.ToInt64();End=$_.BaseAddress.ToInt64()+$_.ModuleMemorySize}})
  $startedAt=(Get-Date).ToString('HH:mm:ss.fff')
  $lines=[HtSamplerAll]::Run($p.Id,$ForS*1000,$IntervalMs,$Depth,[long[]]@($mods.Base),[long[]]@($mods.End))
  # Raw hex addresses + module map; hitch_analyze.py converts and symbolizes (PowerShell is too slow).
  $mods | ForEach-Object {'{0} {1:X} {2:X}' -f $_.Name,$_.Base,$_.End} | Set-Content (Join-Path $Out 'modules.txt')
  "# sampling started at $startedAt (process start $($p.StartTime.ToString('HH:mm:ss.fff')))" | Set-Content (Join-Path $Out 'all-threads.txt')
  [System.IO.File]::AppendAllLines((Join-Path $Out 'all-threads.txt'),[string[]]$lines)
}finally{
  $job | Wait-Job -Timeout 300 | Out-Null
  Receive-Job $job 2>&1 | Out-String | Set-Content (Join-Path $Out 'corrida-resultado.txt')
  Remove-Job $job -Force
}
