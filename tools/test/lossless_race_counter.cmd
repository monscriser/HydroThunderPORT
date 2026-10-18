@echo off
rem Proof run: LS stays in the tray but draws its own counter.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0lossless_race.ps1" -ShowLsCounter 1
