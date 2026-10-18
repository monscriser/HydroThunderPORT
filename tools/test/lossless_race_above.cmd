@echo off
rem Same as lossless_race.cmd, with "above display refresh" enabled.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0lossless_race.ps1" -AboveRefresh 1
