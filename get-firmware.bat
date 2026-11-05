@echo off
rem Downloads the CI-built firmware image from the latest GitHub release.
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0get-firmware.ps1" %*
pause
