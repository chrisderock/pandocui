@echo off
rem Startet build.ps1 unabhaengig von der PowerShell-Ausfuehrungsrichtlinie.
rem Beispiel: build.cmd -Config Debug -Run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
exit /b %ERRORLEVEL%
