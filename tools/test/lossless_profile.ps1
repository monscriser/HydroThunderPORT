param([string]$OutputRoot)
$ErrorActionPreference='Stop'
$OutputRoot=[System.IO.Path]::GetFullPath($OutputRoot)
$oldLocal=$env:LOCALAPPDATA
try {
  $fixtureLocal=Join-Path $OutputRoot 'profile-fixture'
  $env:LOCALAPPDATA=$fixtureLocal
  $dir=Join-Path $env:LOCALAPPDATA 'Lossless Scaling'
  [void][System.IO.Directory]::CreateDirectory($dir)
  $cfg=Join-Path $dir 'Settings.xml'
  $helper=Join-Path $OutputRoot 'profile-helper.ps1'
  $log=Join-Path $OutputRoot 'profile-test-helper.log'
  $title='Hydro Thunder Hurricane: Recompiled'
  function RunHelper([int]$enabled,[int]$mult=2,[int]$above=0) {
    & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper -Enabled $enabled -Multiplier $mult -AboveRefresh $above -Log $log
    if($LASTEXITCODE -ne 0){throw "Helper failed: $(Get-Content -LiteralPath $log -Raw)"}
  }
  function Require([bool]$ok,[string]$message){if(!$ok){throw $message}}
  # A first install has no Lossless Scaling settings folder. The launcher must
  # create a valid seed and the game profile without asking the user to open LS.
  $env:LOCALAPPDATA=Join-Path $OutputRoot ('profile-first-run-'+[guid]::NewGuid().ToString('N'))
  $firstCfg=Join-Path $env:LOCALAPPDATA 'Lossless Scaling\Settings.xml'
  RunHelper 1 2
  Require (Test-Path -LiteralPath $firstCfg) 'First-run Settings.xml was bootstrapped'
  [xml]$first=Get-Content -LiteralPath $firstCfg -Raw
  $firstProfile=@($first.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq $title})
  Require ($firstProfile.Count -eq 1) 'First-run profile is added exactly once'
  Require ($firstProfile[0].AutoScale -eq 'true' -and $firstProfile[0].Path -eq 'hydrothunder.exe' -and !$firstProfile[0].SelectSingleNode('GamePath') -and $firstProfile[0].SelectSingleNode('Title').NextSibling.Name -eq 'Path' -and $firstProfile[0].FrameGeneration -eq 'LSFG3' -and $firstProfile[0].LSFG3Multiplier -eq '2' -and $firstProfile[0].DrawFps -eq 'false') 'First-run profile filters the game and requests LSFG3; FPS is shown by the game overlay'
  Require ($first.Settings.SetupPhase -eq '5' -and $first.Settings.StartAsAdmin -eq 'false') 'Bootstrap uses initialized, non-elevated defaults'
  $firstBytes=[Convert]::ToBase64String([System.IO.File]::ReadAllBytes($firstCfg))
  RunHelper 1 2
  Require ($firstBytes -eq [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($firstCfg))) 'First-run setup is idempotent'
  $env:LOCALAPPDATA=$fixtureLocal
  @'
<Settings><UnknownGlobal>preserve</UnknownGlobal><GameProfiles><Profile><Title>Default</Title><AutoScale>false</AutoScale><ScalingType>Auto</ScalingType><FrameGeneration>Off</FrameGeneration><LSFG3Mode1>FIXED</LSFG3Mode1><LSFG3Multiplier>2</LSFG3Multiplier><UnknownProfile>preserve</UnknownProfile></Profile><Profile><Title>Other game</Title><AutoScale>true</AutoScale></Profile></GameProfiles></Settings>
'@ | Set-Content -LiteralPath $cfg -Encoding UTF8
  RunHelper 1 3
  [xml]$xml=Get-Content -LiteralPath $cfg -Raw
  $ours=@($xml.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq $title})
  Require ($ours.Count -eq 1) 'Exactly one own profile'
  Require ($ours[0].AutoScale -eq 'true' -and $ours[0].LSFG3Multiplier -eq '3' -and $ours[0].FrameGeneration -eq 'LSFG3') 'LSFG3 automatic profile'
  Require ($ours[0].Path -eq 'hydrothunder.exe' -and !$ours[0].SelectSingleNode('GamePath') -and $ours[0].SelectSingleNode('Title').NextSibling.Name -eq 'Path') 'Automatic profile filter uses the LS 3.2.2 <Path> field right after <Title>'
  Require ($ours[0].DrawFps -eq 'false') 'LS counter hidden; the game overlay shows frame generation'
  Require ($xml.Settings.UnknownGlobal -eq 'preserve' -and $ours[0].UnknownProfile -eq 'preserve') 'Preserve unknown settings'
  Require ((@($xml.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq 'Other game'}))[0].AutoScale -eq 'true') 'Other game untouched'
  $before=[System.IO.File]::ReadAllBytes($cfg)
  RunHelper 1 3
  Require ([Convert]::ToBase64String($before) -eq [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($cfg))) 'Idempotent setup'
  # Add synchronization fields just for this isolated schema fixture.
  [xml]$xml=Get-Content -LiteralPath $cfg -Raw
  $ours=(@($xml.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq $title}))[0]
  foreach($pair in @(@('SyncMode','VSYNC2'),@('MaxFrameLatency','1'))){$element=$xml.CreateElement($pair[0]);$element.InnerText=$pair[1];[void]$ours.AppendChild($element)}
  $xml.Save($cfg)
  RunHelper 1 3 1
  [xml]$xml=Get-Content -LiteralPath $cfg -Raw
  $ours=(@($xml.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq $title}))[0]
  Require ($ours.SyncMode -eq 'DEFAULT' -and $ours.MaxFrameLatency -eq '3') 'Above-refresh requests default sync, without forced tearing'
  RunHelper 1 3 0
  [xml]$xml=Get-Content -LiteralPath $cfg -Raw
  $ours=(@($xml.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq $title}))[0]
  Require ($ours.SyncMode -eq 'VSYNC2' -and $ours.MaxFrameLatency -eq '1') 'Restore previous synchronization settings'
  RunHelper 0 3
  [xml]$xml=Get-Content -LiteralPath $cfg -Raw
  Require ((@($xml.Settings.GameProfiles.Profile | Where-Object {$_.Title -eq $title}))[0].AutoScale -eq 'false') 'Disable previous automatic activation'
  Require (@(Get-ChildItem -LiteralPath $dir -Filter '*.bak').Count -ge 2) 'Backups created'
  '<Settings><GameProfiles><Profile><Title>Default</Title></Profile></GameProfiles></Settings>' | Set-Content -LiteralPath $cfg
  $before=[System.IO.File]::ReadAllBytes($cfg)
  & powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper -Enabled 1 -Multiplier 2 -Log $log
  Require ($LASTEXITCODE -ne 0) 'Reject incompatible schema'
  Require ([Convert]::ToBase64String($before) -eq [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($cfg))) 'Unsupported schema left intact'
  'PASS: first-run bootstrap without opening LS, automatic profile creation, XML profile, LS counter hidden, above-refresh/restore, preservation, idempotence, disabling, backups, unsupported-schema protection'
}finally{$env:LOCALAPPDATA=$oldLocal}
