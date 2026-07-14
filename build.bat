@echo off
setlocal
cd /d "%~dp0"
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%POWERSHELL%" set "POWERSHELL=powershell.exe"

"%POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\build-installer-x64.ps1" -Configuration Release
if errorlevel 1 (
    echo.
    echo Build failed. See the error above.
    pause
)
exit /b %ERRORLEVEL%
