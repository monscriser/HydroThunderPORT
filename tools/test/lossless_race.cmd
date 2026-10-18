@echo off
rem Opened through explorer.exe so the test runs outside packaged-app virtualization.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0lossless_race.ps1" %*
