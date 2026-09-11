@echo off
title Game Detector - Smart Context Mode installieren

rem Startet install.ps1 mit Administratorrechten. Das Plugin muss in den
rem OBS-Programmordner, und dorthin darf nur ein Administrator schreiben.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo   Administratorrechte werden angefordert...
    echo   Bitte die Windows-Abfrage mit "Ja" bestaetigen.
    echo.
    powershell -NoProfile -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
