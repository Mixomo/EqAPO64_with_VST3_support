@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-smoke.ps1" %*
set "SMOKE_EXIT=%ERRORLEVEL%"
pause
exit /b %SMOKE_EXIT%
