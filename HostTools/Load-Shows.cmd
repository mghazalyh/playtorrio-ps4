@echo off
title PlayTorrio PS4 - Load real TV shows and movies
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Refresh-Catalog.ps1" %*
echo.
echo Press any key to close the catalog loader.
pause >nul
