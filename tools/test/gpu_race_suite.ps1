# Runs gpu_race.ps1 for several configurations: "name|toml k=v;...|cvars k=v;..."
param([string[]]$Tests)
foreach($t in $Tests){$name,$set,$cv=$t -split '\|',3;& (Join-Path $PSScriptRoot 'gpu_race.ps1') -Name $name -Set "$set" -Cvars "$cv"}
