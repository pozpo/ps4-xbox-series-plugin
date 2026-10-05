@echo off
REM Double-click to check that everything needed for the build is installed.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Check
echo.
pause
