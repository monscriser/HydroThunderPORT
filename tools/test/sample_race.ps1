# Sampling profile INSIDE a race (measurement only). Runs gpu_race.ps1 (virtual pad,
# isolated game-test copy, toml restored) in a background job, attaches to the game
# once the race has been running -Into seconds, measures CPU per thread for 5 s, then
# samples the -Top busiest threads (HtSampler: RIP + stack return candidates).
# Symbolize afterwards in WSL with tools/test/symbolize_samples.py.
# The sampler suspends threads briefly: use the profile for WHERE time goes, never
# as a FPS/CPU benchmark (the race result.json of this run is not a benchmark).
param([Parameter(Mandatory)][string]$Out,[string]$Set='',[string]$Cvars='',
      [int]$MenuSeconds=75,[int]$Into=25,[int]$Top=5,[int]$Samples=600,[int]$Depth=8)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
Add-Type -Path (Join-Path $PSScriptRoot 'HtSampler.cs')
$race=Join-Path $PSScriptRoot 'gpu_race.ps1'
$job=Start-Job -ScriptBlock {param($r,$o,$s,$c,$m)& $r -Name 'corrida' -OutRoot $o -Set $s -Cvars $c -MenuSeconds $m} -ArgumentList $race,$Out,$Set,$Cvars,$MenuSeconds
try{
  $p=$null;$t=Get-Date
  while(!$p -and ((Get-Date)-$t).TotalSeconds -lt 60){Start-Sleep 1;$p=Get-Process hydrothunder -ErrorAction SilentlyContinue | Select-Object -First 1}
  if(!$p){throw 'game did not start'}
  # gpu_race: 15 s boot + MenuSeconds of menus, then the race starts.
  $raceAt=$p.StartTime.AddSeconds(15+$MenuSeconds+$Into)
  while((Get-Date) -lt $raceAt -and !$p.HasExited){Start-Sleep 1}
  $p.Refresh()
  $mods=@($p.Modules | ForEach-Object {[pscustomobject]@{Name=$_.ModuleName;Base=$_.BaseAddress.ToInt64();End=$_.BaseAddress.ToInt64()+$_.ModuleMemorySize}})
  $c0=$p.TotalProcessorTime.TotalMilliseconds
  $t0=@{};foreach($th in $p.Threads){try{$t0[$th.Id]=$th.TotalProcessorTime.TotalMilliseconds}catch{}}
  Start-Sleep 5;$p.Refresh()
  $all=@($p.Threads | ForEach-Object {try{[pscustomobject]@{Id=$_.Id;MsPerS=[math]::Round(($_.TotalProcessorTime.TotalMilliseconds-$t0[$_.Id])/5,1)}}catch{}} | Sort-Object MsPerS -Descending)
  "processo: {0} ms CPU/s" -f [math]::Round(($p.TotalProcessorTime.TotalMilliseconds-$c0)/5,1) | Tee-Object (Join-Path $Out 'threads.txt')
  $all | Select-Object -First 15 | Format-Table -AutoSize | Out-String | Tee-Object (Join-Path $Out 'threads.txt') -Append
  $mb=[long[]]@($mods.Base);$me=[long[]]@($mods.End)
  foreach($b in ($all | Select-Object -First $Top)){
    $res=[HtSampler]::Sample($p.Id,$b.Id,$Samples,2,$Depth,$mb,$me)
    $lines=foreach($s in $res){ ($s | ForEach-Object { $a=$_; if(!$a){''}else{ $m=$mods | Where-Object {$a -ge $_.Base -and $a -lt $_.End} | Select-Object -First 1; if($m){'{0}+0x{1:X}' -f $m.Name,($a-$m.Base)}else{'0x{0:X}' -f $a} } }) -join ' ' }
    $lines | Set-Content (Join-Path $Out ("thread-{0}-cpu{1}ms.txt" -f $b.Id,[math]::Round($b.MsPerS)))
    "thread $($b.Id): $($b.MsPerS) ms CPU/s, $($res.Count) amostras"
  }
}finally{
  $job | Wait-Job -Timeout 300 | Out-Null
  Receive-Job $job | Out-String | Set-Content (Join-Path $Out 'corrida-resultado.txt')
  Remove-Job $job -Force
}
