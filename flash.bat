@echo off
rem Runs flash.ps1 (plain text - read it) in Windows' built-in 32-bit PowerShell,
rem which is required to load GigaDevice's 32-bit GD_DFU_DLL.dll.
set "PS=%SystemRoot%\SysWOW64\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1" %*
pause
