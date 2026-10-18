@echo off
rem Proof run, above display refresh: LS stays in the tray but draws its own counter.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0lossless_race.ps1" -AboveRefresh 1 -ShowLsCounter 1
