# Averages the engine's own [ht-perf] GPU breakdown over the measured race window
# of gpu_race.ps1 runs. Usage: perf_breakdown.ps1 <run dir> [<run dir> ...]
param([Parameter(Mandatory,ValueFromRemainingArguments)][string[]]$Runs)
$inv=[Globalization.CultureInfo]::InvariantCulture
$keys='total','transfers','resolve','texload','upload','draws','rp','subm','barriers','texloads','resolves','uploads','upload_kb','rp_breaks_barrier'
foreach($run in $Runs){
  $lines=@(Get-Content (Join-Path $run 'game.log') | Where-Object {$_ -match '\[ht-perf\] t='})
  # Same window as gpu_race.ps1: the last ~35 s of reports are inside the race.
  $lines=@($lines | Select-Object -Last 17 | Select-Object -SkipLast 1)
  $row=[ordered]@{Teste=Split-Path $run -Leaf}
  foreach($k in $keys){
    $v=@($lines | ForEach-Object {$m=[regex]::Match($_,"[ (]$k=([\d.]+)");if($m.Success){[double]::Parse($m.Groups[1].Value,$inv)}})
    $row[$k]=if($v.Count){[math]::Round(($v|Measure-Object -Average).Average,2)}else{''}
  }
  [pscustomobject]$row
}
