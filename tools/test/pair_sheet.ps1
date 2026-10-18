# Side-by-side contact sheets of two race runs (same shot names): left = A, right = B.
# usage: pair_sheet.ps1 -A <dir> -B <dir> -Out <dir> [-Width 640] [-PerSheet 4]
param([Parameter(Mandatory)][string]$A,[Parameter(Mandatory)][string]$B,[Parameter(Mandatory)][string]$Out,
      [int]$Width=640,[int]$PerSheet=4,[string]$Filter='race-*.png')
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$names=@(Get-ChildItem $A -Filter $Filter | Sort-Object {[int]($_.BaseName -replace '\D','')} | ForEach-Object Name | Where-Object { Test-Path (Join-Path $B $_) })
$font=New-Object Drawing.Font 'Arial',14,([Drawing.FontStyle]::Bold)
for($s=0;$s -lt $names.Count;$s+=$PerSheet){
  $chunk=$names[$s..([math]::Min($s+$PerSheet,$names.Count)-1)]
  $first=[Drawing.Image]::FromFile((Join-Path $A $chunk[0]));$h=[int]($first.Height*$Width/$first.Width);$first.Dispose()
  $sheet=New-Object Drawing.Bitmap ($Width*2+4),(($h+2)*$chunk.Count);$g=[Drawing.Graphics]::FromImage($sheet);$g.Clear([Drawing.Color]::Black)
  for($i=0;$i -lt $chunk.Count;$i++){
    foreach($side in 0,1){
      $img=[Drawing.Image]::FromFile((Join-Path @($A,$B)[$side] $chunk[$i]))
      $g.DrawImage($img,$side*($Width+4),$i*($h+2),$Width,$h);$img.Dispose()
      $g.DrawString(("{0} {1}" -f @('A','B')[$side],$chunk[$i]),$font,[Drawing.Brushes]::Yellow,$side*($Width+4)+4,$i*($h+2)+4)
    }
  }
  $file=Join-Path $Out ("par-{0:D2}.jpg" -f ($s/$PerSheet+1));$sheet.Save($file,[Drawing.Imaging.ImageFormat]::Jpeg);$g.Dispose();$sheet.Dispose();$file
}
