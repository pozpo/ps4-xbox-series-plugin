@echo off
REM Double-click to download the OpenOrbis toolchain and the GoldHEN SDK.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" %*
echo.
pause
