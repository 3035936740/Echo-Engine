@echo off
setlocal
rem Double-click builds Release and retains the output window.
rem Use -NoPause in automation; all arguments are passed to build.ps1.
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Pause %*
exit /b %errorlevel%
