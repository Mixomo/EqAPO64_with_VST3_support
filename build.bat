@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set "BUILD_EXIT=%ERRORLEVEL%"
echo.
if not "%BUILD_EXIT%"=="0" echo Compilacion interrumpida. Revisa el mensaje y los logs.
pause
exit /b %BUILD_EXIT%
